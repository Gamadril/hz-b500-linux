use std::{
    io,
    pin::Pin,
    task::{Context, Poll, ready},
};

use nusb::{
    Endpoint,
    transfer::{Buffer, Bulk, Completion, EndpointDirection, In, Out, TransferError},
};
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};

#[repr(u16)]
enum AoaStringIndex {
    Manufacturer = 0,
    Model = 1,
    Description = 2,
    Version = 3,
    Uri = 4,
    SerialNumber = 5,
}

async fn send_aoa_string(device: &nusb::Device, index: u16, value: &str) {
    device
        .control_out(
            nusb::transfer::ControlOut {
                control_type: nusb::transfer::ControlType::Vendor,
                recipient: nusb::transfer::Recipient::Device,
                request: 52,
                value: 0,
                data: value.as_bytes(),
                index,
            },
            std::time::Duration::from_millis(1000),
        )
        .await
        .unwrap();
}

pub async fn identify_accessory(device: &nusb::Device) {
    send_aoa_string(device, AoaStringIndex::Manufacturer as u16, "Android").await;
    send_aoa_string(device, AoaStringIndex::Model as u16, "Android Auto").await;
    send_aoa_string(device, AoaStringIndex::Description as u16, "Android Auto").await;
    send_aoa_string(device, AoaStringIndex::Version as u16, "2.0.1").await;
    send_aoa_string(device, AoaStringIndex::Uri as u16, "").await;
    send_aoa_string(device, AoaStringIndex::SerialNumber as u16, "HU-AAAAAA").await;
}

pub async fn accessory_start(device: &nusb::Device) {
    device
        .control_out(
            nusb::transfer::ControlOut {
                control_type: nusb::transfer::ControlType::Vendor,
                recipient: nusb::transfer::Recipient::Device,
                request: 53,
                value: 0,
                data: &[],
                index: 0,
            },
            std::time::Duration::from_millis(1000),
        )
        .await
        .unwrap();
}

pub async fn wait_for_accessory() -> Result<nusb::Device, nusb::Error> {
    loop {
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;

        if let Ok(devices) = nusb::list_devices().await {
            for info in devices {
                if info.vendor_id() == 0x18d1
                    && (info.product_id() == 0x2D00 || info.product_id() == 0x2D01)
                {
                    log::info!("About to open {:?}", info);
                    return info.open().await;
                }
            }
        }
        log::info!("Didnt find accessory");
    }
}

/// Determines if the usb device is an android device
pub fn is_android_device(info: &nusb::DeviceInfo) -> bool {
    // Already in accessory mode - best case
    if info.vendor_id() == 0x18D1 && matches!(info.product_id(), 0x2D00 | 0x2D01) {
        return true;
    }

    // Has an ADB interface (class 0xFF, subclass 0x42, protocol 0x01)
    if info
        .interfaces()
        .any(|i| i.class() == 0xFF && i.subclass() == 0x42 && i.protocol() == 0x01)
    {
        return true;
    }

    // MTP/PTP mode (what your Pixel 7 showed)
    if info
        .interfaces()
        .any(|i| i.class() == 0x06 && i.subclass() == 0x01)
    {
        return true;
    }

    false
}

/// if possible, get the aoa protocol number from the device
pub async fn get_aoa_protocol(dev: &nusb::Device) -> Option<u16> {
    let result = dev
        .control_in(
            nusb::transfer::ControlIn {
                control_type: nusb::transfer::ControlType::Vendor,
                recipient: nusb::transfer::Recipient::Device,
                request: 51,
                value: 0,
                index: 0,
                length: 2,
            },
            std::time::Duration::from_millis(1000),
        )
        .await;
    if let Ok(r) = result {
        let version = u16::from_le_bytes([r[0], r[1]]);
        if version >= 1 { Some(version) } else { None }
    } else {
        None
    }
}

pub async fn claim_aoa_interface(device: &nusb::Device) -> nusb::Interface {
    // AOA uses interface 0, with one bulk-in and one bulk-out endpoint
    device.claim_interface(0).await.unwrap()
}

pub struct AndroidAutoUsb {
    ep_in: UsbReader,
    ep_out: UsbWriter,
}

impl AndroidAutoUsb {
    /// construct a new interface to the android usb device
    pub fn new(interface: nusb::Interface) -> Option<Self> {
        if let Ok(w) = interface.endpoint::<nusb::transfer::Bulk, nusb::transfer::In>(0x81) {
            let a = UsbReader(HeapReader::new(w));
            if let Ok(w) = interface.endpoint::<nusb::transfer::Bulk, nusb::transfer::Out>(0x1) {
                let b = UsbWriter(HeapWriter::new(w));
                return Some(Self {
                    ep_in: a,
                    ep_out: b,
                });
            }
        }
        None
    }

    /// split the struct
    pub fn into_split(self) -> (UsbReader, UsbWriter) {
        (self.ep_in, self.ep_out)
    }
}

// Endpoint::allocate() can mmap strongly ordered DMA memory on the F133.
// memcpy from a partially consumed buffer can then fault on an unaligned load.
// Always use Buffer::new(), including for OUT buffers whose writes may also be
// unaligned. Keep one reusable heap buffer per direction and one transfer in flight.
const TRANSFER_SIZE: usize = 4096;

trait TransferQueue {
    fn max_packet_size(&self) -> usize;
    fn pending(&self) -> usize;
    fn submit(&mut self, buffer: Buffer);
    fn poll_complete(&mut self, cx: &mut Context<'_>) -> Poll<Completion>;
}

impl<D: EndpointDirection> TransferQueue for Endpoint<Bulk, D> {
    fn max_packet_size(&self) -> usize {
        self.max_packet_size()
    }

    fn pending(&self) -> usize {
        self.pending()
    }

    fn submit(&mut self, buffer: Buffer) {
        self.submit(buffer);
    }

    fn poll_complete(&mut self, cx: &mut Context<'_>) -> Poll<Completion> {
        self.poll_next_complete(cx)
    }
}

fn transfer_size(queue: &impl TransferQueue) -> usize {
    TRANSFER_SIZE.div_ceil(queue.max_packet_size()) * queue.max_packet_size()
}

/// USB byte-stream reader backed by cacheable heap memory.
pub struct UsbReader(HeapReader<Endpoint<Bulk, In>>);

impl AsyncRead for UsbReader {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        Pin::new(&mut self.0).poll_read(cx, buf)
    }
}

struct HeapReader<Q> {
    queue: Q,
    reading: Option<Completion>,
    position: usize,
    transfer_size: usize,
}

impl<Q: TransferQueue> HeapReader<Q> {
    fn new(queue: Q) -> Self {
        let transfer_size = transfer_size(&queue);
        Self {
            queue,
            reading: None,
            position: 0,
            transfer_size,
        }
    }
}

impl<Q: TransferQueue + Unpin> AsyncRead for HeapReader<Q> {
    fn poll_read(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        let this = self.get_mut();
        if buf.remaining() == 0 {
            return Poll::Ready(Ok(()));
        }

        // A successful USB ZLP is not stream EOF. Bound work per poll in case
        // the device sends a continuous stream of zero-length packets.
        for _ in 0..16 {
            if let Some(completion) = &this.reading {
                let remaining = &completion.buffer[this.position..];
                if !remaining.is_empty() {
                    let len = remaining.len().min(buf.remaining());
                    buf.put_slice(&remaining[..len]);
                    this.position += len;
                    return Poll::Ready(Ok(()));
                }
                // Deliver any partial data before reporting its transfer error.
                if let Err(error) = completion.status {
                    return Poll::Ready(Err(error.into()));
                }
            }

            if this.queue.pending() == 0 {
                let buffer = this
                    .reading
                    .take()
                    .map(|completion| completion.buffer)
                    .unwrap_or_else(|| Buffer::new(this.transfer_size));
                this.queue.submit(buffer);
            }
            this.reading = Some(ready!(this.queue.poll_complete(cx)));
            this.position = 0;
        }
        cx.waker().wake_by_ref();
        Poll::Pending
    }
}

/// Buffered USB byte-stream writer backed by cacheable heap memory.
pub struct UsbWriter(HeapWriter<Endpoint<Bulk, Out>>);

impl AsyncWrite for UsbWriter {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.0).poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.0).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        self.as_mut().poll_flush(cx)
    }
}

#[derive(Clone, Copy)]
enum WriteFailure {
    Transfer(TransferError),
    Short,
}

impl From<WriteFailure> for io::Error {
    fn from(error: WriteFailure) -> Self {
        match error {
            WriteFailure::Transfer(error) => error.into(),
            WriteFailure::Short => io::Error::new(io::ErrorKind::WriteZero, "short USB write"),
        }
    }
}

struct HeapWriter<Q> {
    queue: Q,
    writing: Option<Buffer>,
    transfer_size: usize,
    failure: Option<WriteFailure>,
}

impl<Q: TransferQueue> HeapWriter<Q> {
    fn new(queue: Q) -> Self {
        let transfer_size = transfer_size(&queue);
        Self {
            queue,
            writing: Some(Buffer::new(transfer_size)),
            transfer_size,
            failure: None,
        }
    }

    fn poll_complete(&mut self, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        let completion = ready!(self.queue.poll_complete(cx));
        self.failure = match completion.status {
            Err(error) => Some(WriteFailure::Transfer(error)),
            Ok(()) if completion.actual_len != completion.buffer.len() => Some(WriteFailure::Short),
            Ok(()) => None,
        };
        let mut buffer = completion.buffer;
        buffer.clear();
        self.writing = Some(buffer);
        Poll::Ready(self.failure.map_or(Ok(()), |error| Err(error.into())))
    }

    fn submit(&mut self) {
        if self
            .writing
            .as_ref()
            .is_some_and(|buffer| !buffer.is_empty())
        {
            self.queue.submit(self.writing.take().unwrap());
        }
    }
}

impl<Q: TransferQueue + Unpin> AsyncWrite for HeapWriter<Q> {
    fn poll_write(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        let this = self.get_mut();
        if let Some(error) = this.failure {
            return Poll::Ready(Err(error.into()));
        }
        if buf.is_empty() {
            return Poll::Ready(Ok(0));
        }
        if this.writing.is_none() {
            ready!(this.poll_complete(cx))?;
        }
        let buffer = this.writing.as_mut().unwrap();
        let len = buf.len().min(this.transfer_size - buffer.len());
        buffer.extend_from_slice(&buf[..len]);
        if buffer.len() == this.transfer_size {
            this.submit();
        }
        Poll::Ready(Ok(len))
    }

    fn poll_flush(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        let this = self.get_mut();
        if let Some(error) = this.failure {
            return Poll::Ready(Err(error.into()));
        }
        this.submit();
        if this.queue.pending() > 0 {
            ready!(this.poll_complete(cx))?;
        }
        Poll::Ready(Ok(()))
    }

    fn poll_shutdown(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        self.poll_flush(cx)
    }
}

#[cfg(test)]
#[path = "usb_tests.rs"]
mod tests;

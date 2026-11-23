use super::*;
use std::collections::VecDeque;

enum Response {
    Read(Vec<u8>, Result<(), TransferError>),
    Write(Result<(), TransferError>, Option<usize>),
}

struct MockQueue {
    responses: VecDeque<Response>,
    pending: Option<Buffer>,
    submitted: Vec<Vec<u8>>,
    pointers: Vec<usize>,
    blocked: bool,
    packet_size: usize,
}

impl MockQueue {
    fn new(responses: impl IntoIterator<Item = Response>) -> Self {
        Self {
            responses: responses.into_iter().collect(),
            pending: None,
            submitted: Vec::new(),
            pointers: Vec::new(),
            blocked: false,
            packet_size: 512,
        }
    }
}

impl TransferQueue for MockQueue {
    fn max_packet_size(&self) -> usize {
        self.packet_size
    }

    fn pending(&self) -> usize {
        usize::from(self.pending.is_some())
    }

    fn submit(&mut self, buffer: Buffer) {
        assert!(self.pending.is_none(), "only one transfer may be in flight");
        assert!(!buffer.is_zero_copy(), "USB buffers must use the heap");
        assert_eq!(buffer.requested_len() % self.packet_size, 0);
        self.pointers.push(buffer.as_ptr() as usize);
        self.submitted.push(buffer.to_vec());
        self.pending = Some(buffer);
    }

    fn poll_complete(&mut self, _: &mut Context<'_>) -> Poll<Completion> {
        assert!(self.pending.is_some());
        if self.blocked {
            return Poll::Pending;
        }
        let mut buffer = self.pending.take().unwrap();
        let (status, actual_len) = match self.responses.pop_front().expect("unexpected transfer") {
            Response::Read(data, status) => {
                assert!(data.len() <= buffer.requested_len());
                buffer.clear();
                buffer.extend_from_slice(&data);
                (status, data.len())
            }
            Response::Write(status, actual_len) => (status, actual_len.unwrap_or(buffer.len())),
        };
        Poll::Ready(Completion {
            buffer,
            status,
            actual_len,
        })
    }
}

fn read(reader: &mut HeapReader<MockQueue>, output: &mut [u8]) -> Poll<io::Result<usize>> {
    let mut cx = Context::from_waker(std::task::Waker::noop());
    let mut buf = ReadBuf::new(output);
    Pin::new(reader)
        .poll_read(&mut cx, &mut buf)
        .map(|result| result.map(|()| buf.filled().len()))
}

fn write(writer: &mut HeapWriter<MockQueue>, data: &[u8]) -> Poll<io::Result<usize>> {
    Pin::new(writer).poll_write(&mut Context::from_waker(std::task::Waker::noop()), data)
}

fn flush(writer: &mut HeapWriter<MockQueue>) -> Poll<io::Result<()>> {
    Pin::new(writer).poll_flush(&mut Context::from_waker(std::task::Waker::noop()))
}

#[test]
fn heap_read_handles_four_byte_header_offset_and_reuses_buffer() {
    let payload: Vec<u8> = (0..2349).map(|index| (index % 251) as u8).collect();
    let header = [0, 3, 9, 45];
    let mut frame = header.to_vec();
    frame.extend_from_slice(&payload);
    let mut reader = HeapReader::new(MockQueue::new([
        Response::Read(frame, Ok(())),
        Response::Read(vec![99], Ok(())),
    ]));
    for expected in header {
        let mut byte = [0];
        assert!(matches!(read(&mut reader, &mut byte), Poll::Ready(Ok(1))));
        assert_eq!(byte[0], expected);
    }
    let mut output = vec![0; payload.len()];
    assert!(matches!(
        read(&mut reader, &mut output),
        Poll::Ready(Ok(2349))
    ));
    assert_eq!(output, payload);
    assert_eq!(reader.queue.submitted.len(), 1);
    let mut byte = [0];
    assert!(matches!(read(&mut reader, &mut byte), Poll::Ready(Ok(1))));
    assert_eq!(byte, [99]);
    assert_eq!(reader.queue.pointers[0], reader.queue.pointers[1]);
}

#[test]
fn read_preserves_pending_transfer_across_dropped_polls() {
    let mut queue = MockQueue::new([Response::Read(vec![1, 2, 3], Ok(()))]);
    queue.blocked = true;
    let mut reader = HeapReader::new(queue);
    let mut output = [0; 8];
    assert!(read(&mut reader, &mut output).is_pending());
    assert!(read(&mut reader, &mut output).is_pending());
    assert_eq!(reader.queue.submitted.len(), 1);
    reader.queue.blocked = false;
    assert!(matches!(read(&mut reader, &mut output), Poll::Ready(Ok(3))));
    assert_eq!(&output[..3], &[1, 2, 3]);
    assert_eq!(reader.queue.submitted.len(), 1);
}

#[test]
fn zero_length_packets_are_not_eof_and_yield_cooperatively() {
    let mut responses: Vec<_> = (0..32).map(|_| Response::Read(vec![], Ok(()))).collect();
    responses.push(Response::Read(vec![7], Ok(())));
    let mut reader = HeapReader::new(MockQueue::new(responses));
    let mut output = [0];
    assert!(read(&mut reader, &mut output).is_pending());
    assert!(read(&mut reader, &mut output).is_pending());
    assert!(matches!(read(&mut reader, &mut output), Poll::Ready(Ok(1))));
    assert_eq!(output, [7]);
}

#[test]
fn read_returns_partial_data_before_persistent_transfer_error() {
    let mut reader = HeapReader::new(MockQueue::new([Response::Read(
        vec![4, 5],
        Err(TransferError::Disconnected),
    )]));
    let mut output = [0; 8];
    assert!(matches!(read(&mut reader, &mut output), Poll::Ready(Ok(2))));
    assert_eq!(&output[..2], &[4, 5]);
    for _ in 0..2 {
        assert!(
            matches!(read(&mut reader, &mut output), Poll::Ready(Err(error))
            if error.kind() == io::ErrorKind::ConnectionAborted)
        );
    }
    assert_eq!(reader.queue.submitted.len(), 1);
}

#[test]
fn empty_read_does_not_submit_and_requests_are_packet_aligned() {
    let mut queue = MockQueue::new([Response::Read(vec![1], Ok(()))]);
    queue.packet_size = 1000;
    let mut reader = HeapReader::new(queue);
    assert!(matches!(read(&mut reader, &mut []), Poll::Ready(Ok(0))));
    assert_eq!(reader.queue.submitted.len(), 0);
    assert!(matches!(read(&mut reader, &mut [0]), Poll::Ready(Ok(1))));
    assert_eq!(
        reader.reading.as_ref().unwrap().buffer.requested_len(),
        5000
    );
}

#[test]
fn buffered_writes_preserve_offsets_and_flush_waits_for_completion() {
    let mut writer = HeapWriter::new(MockQueue::new([
        Response::Write(Ok(()), None),
        Response::Write(Ok(()), None),
    ]));
    assert!(matches!(
        write(&mut writer, &[0, 3, 0, 3]),
        Poll::Ready(Ok(4))
    ));
    assert!(matches!(write(&mut writer, &[5, 6, 7]), Poll::Ready(Ok(3))));
    assert!(writer.queue.submitted.is_empty());
    writer.queue.blocked = true;
    assert!(flush(&mut writer).is_pending());
    assert!(flush(&mut writer).is_pending());
    assert_eq!(writer.queue.submitted, [vec![0, 3, 0, 3, 5, 6, 7]]);
    writer.queue.blocked = false;
    assert!(matches!(flush(&mut writer), Poll::Ready(Ok(()))));
    assert!(matches!(write(&mut writer, &[9]), Poll::Ready(Ok(1))));
    assert!(matches!(flush(&mut writer), Poll::Ready(Ok(()))));
    assert_eq!(writer.queue.submitted[1], [9]);
    assert_eq!(writer.queue.pointers[0], writer.queue.pointers[1]);
}

#[test]
fn full_write_applies_backpressure_and_shutdown_flushes_tail() {
    let mut writer = HeapWriter::new(MockQueue::new([
        Response::Write(Ok(()), None),
        Response::Write(Ok(()), None),
    ]));
    let data = vec![42; TRANSFER_SIZE + 1];
    assert!(matches!(
        write(&mut writer, &data),
        Poll::Ready(Ok(TRANSFER_SIZE))
    ));
    writer.queue.blocked = true;
    assert!(write(&mut writer, &[43]).is_pending());
    assert_eq!(writer.queue.submitted.len(), 1);
    writer.queue.blocked = false;
    assert!(matches!(write(&mut writer, &[43]), Poll::Ready(Ok(1))));
    let mut cx = Context::from_waker(std::task::Waker::noop());
    writer.queue.blocked = true;
    assert!(Pin::new(&mut writer).poll_shutdown(&mut cx).is_pending());
    writer.queue.blocked = false;
    assert!(matches!(
        Pin::new(&mut writer).poll_shutdown(&mut cx),
        Poll::Ready(Ok(()))
    ));
    assert_eq!(writer.queue.submitted, [vec![42; TRANSFER_SIZE], vec![43]]);
}

#[test]
fn failed_and_short_writes_remain_errors_after_flush() {
    for (status, actual, expected) in [
        (
            Err(TransferError::Disconnected),
            None,
            io::ErrorKind::ConnectionAborted,
        ),
        (Ok(()), Some(2), io::ErrorKind::WriteZero),
    ] {
        let mut writer = HeapWriter::new(MockQueue::new([Response::Write(status, actual)]));
        assert!(matches!(write(&mut writer, &[1, 2, 3]), Poll::Ready(Ok(3))));
        assert!(matches!(flush(&mut writer), Poll::Ready(Err(error)) if error.kind() == expected));
        assert!(
            matches!(write(&mut writer, &[4]), Poll::Ready(Err(error)) if error.kind() == expected)
        );
        assert_eq!(writer.queue.submitted.len(), 1);
    }
}

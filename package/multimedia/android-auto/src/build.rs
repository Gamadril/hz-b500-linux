fn main() {
    println!("cargo:rerun-if-env-changed=CEDAR_SDK_ROOT");
    println!("cargo:rerun-if-env-changed=CEDAR_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=SUNXI_INCLUDE_DIR");
    println!("cargo:rerun-if-changed=native/cedar_sink.c");
    println!("cargo:rerun-if-changed=native/cedar_sink.h");
    println!("cargo:rerun-if-changed=native/alsa_sink.c");
    println!("cargo:rerun-if-changed=native/alsa_sink.h");
    println!("cargo:rerun-if-changed=native/touch_input.c");
    println!("cargo:rerun-if-changed=native/media_input.c");
    println!("cargo:rerun-if-changed=native/touch_input.h");
    if std::env::var_os("CARGO_FEATURE_CEDAR").is_some() {
        assert_eq!(
            std::env::var("CARGO_CFG_TARGET_OS").unwrap(),
            "linux",
            "Cedar requires Linux"
        );
        let root = std::env::var_os("CEDAR_SDK_ROOT").map(std::path::PathBuf::from);
        let cedar = std::env::var_os("CEDAR_INCLUDE_DIR")
            .map(std::path::PathBuf::from)
            .or_else(|| {
                root.as_ref()
                    .map(|p| p.join("package/allwinner/tina_multimedia/libcedarc/include"))
            })
            .expect("set CEDAR_SDK_ROOT or CEDAR_INCLUDE_DIR to the matching CedarC SDK headers");
        let sunxi = std::env::var_os("SUNXI_INCLUDE_DIR")
            .map(std::path::PathBuf::from)
            .or_else(|| root.as_ref().map(|p| p.join("lichee/linux-5.4/include")))
            .expect("set CEDAR_SDK_ROOT or SUNXI_INCLUDE_DIR (contains video/sunxi_display2.h)");
        cc::Build::new()
            .file("native/cedar_sink.c")
            .include(cedar)
            .flag("-idirafter")
            .flag(sunxi.to_str().expect("non-UTF8 Sunxi header path"))
            .flag_if_supported("-std=c11")
            .warnings(true)
            .compile("aa_cedar_sink");
        cc::Build::new()
            .file("native/alsa_sink.c")
            .flag_if_supported("-std=c11")
            .warnings(true)
            .compile("aa_alsa_sink");
        cc::Build::new()
            .file("native/touch_input.c")
            .file("native/media_input.c")
            .flag_if_supported("-std=c11")
            .warnings(true)
            .compile("aa_touch_input");
        println!("cargo:rustc-link-lib=dl");
        println!("cargo:rustc-link-lib=pthread");
    }
    let out_dir_env = std::env::var_os("OUT_DIR").unwrap();
    let out_dir = std::path::Path::new(&out_dir_env);
    protobuf_codegen::Codegen::new()
        .out_dir(out_dir)
        .protoc()
        .includes(["protobuf"])
        .input("protobuf/Bluetooth.proto")
        .input("protobuf/StockBluetooth.proto")
        .input("protobuf/Wifi.proto")
        .cargo_out_dir("protobuf")
        .run_from_script();
}

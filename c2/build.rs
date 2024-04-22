use std::env;

fn main() {
    groth16_cuda();
}

fn groth16_cuda() {
    let supported_sms = cmd_lib::run_fun!(
        bash -c "nvcc --help | sed -n -e '/gpu-architecture <arch>/,/gpu-code <code>/ p' | sed -n -e '/Allowed values/,/gpu-code <code>/ p' | grep -i sm_ | grep -Eo 'sm_[0-9]+' | sed -e s/sm_//g | sort -g -u | tr '\n' ' '"
    ).unwrap();
    let supported_sms = supported_sms.strip_suffix(' ').unwrap().split(' ');

    let mut nvcc = cc::Build::new();
    nvcc.cuda(true);

    for sm in supported_sms {
        match sm.parse::<u32>() {
            Ok(sm_u32) if sm_u32 >= 50 => {}
            _ => continue,
        }
        nvcc.flag("-gencode")
            .flag(format!("arch=compute_{},code=sm_{}", sm, sm).as_str());
    }

    nvcc.flag("-t0");
    nvcc.define("TAKE_RESPONSIBILITY_FOR_ERROR_MESSAGE", None);
    nvcc.define("FEATURE_BLS12_381", None);
    apply_blst_flags(&mut nvcc);
    if let Some(include) = env::var_os("DEP_BLST_C_SRC") {
        nvcc.include(&include);
    }
    if let Some(include) = env::var_os("DEP_SPPARK_ROOT") {
        nvcc.include(include);
    }
    nvcc.flag("-Xcompiler").flag("-Wno-subobject-linkage");
    nvcc.flag("-Xcompiler").flag("-Wno-unused-function");

    nvcc.file("cuda/groth16_cuda.cu").compile("groth16_cuda");

    println!("cargo:rerun-if-changed=cuda");
    println!("cargo:rerun-if-env-changed=CXXFLAGS");
}

fn apply_blst_flags(nvcc: &mut cc::Build) {
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap();

    match (cfg!(feature = "portable"), cfg!(feature = "force-adx")) {
        (true, false) => {
            nvcc.define("__BLST_PORTABLE__", None);
        }
        (false, true) => {
            if target_arch.eq("x86_64") {
                nvcc.define("__ADX__", None);
            }
        }
        (false, false) =>
        {
            #[cfg(target_arch = "x86_64")]
            if target_arch.eq("x86_64") && std::is_x86_feature_detected!("adx") {
                nvcc.define("__ADX__", None);
            }
        }
        (true, true) => panic!("Cannot compile with both `portable` and `force-adx` features"),
    }
}

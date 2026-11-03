{
  # lzf_fault_injection=1 is the test-only coverage build (scripts/coverage-native.js):
  # it compiles the fault hooks in src/lzf.cc and turns optimisation off so line data
  # maps 1:1 to the source. Releases and prebuilds always use the default 0.
  "variables": { "lzf_fault_injection%": 0 },
  "targets": [
    {
      "target_name": "lzf",
      "sources": [
        "src/lzf.cc",
        "src/lzf/lzf_c.cc",
        "src/lzf/lzf_d.cc"
      ],
      "include_dirs": [
        "<!(node -p \"require('node-addon-api').include_dir\")",
        "src/lzf"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      # -Wno-implicit-fallthrough: upstream liblzf (src/lzf/lzf_d.cc) deliberately unrolls its
      # copy loops with "case" fallthrough (Duff's device); the warning gcc enables with -Wextra
      # is not a bug here. The vendored code and our own code (src/lzf.cc) build as a single
      # target, so the suppression is target-wide; we do not rewrite the algorithm. MSVC does
      # not emit this warning, so no flag is added on Windows.
      "conditions": [
        [
          'OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris"',
          {
            "cflags": [ "-O3", "-Wno-implicit-fallthrough" ],
            "cflags_cc": [ "-O3" ],
            "conditions": [
              ["target_arch=='x64'", { "cflags": [ "-fPIC" ] }]
            ]
          }
        ],
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "OTHER_CFLAGS": [ "-O3", "-Wno-implicit-fallthrough" ]
            }
          }
        ],
        [
          "lzf_fault_injection==1",
          { "defines": [ "NODE_LZF_FAULT_INJECTION" ] }
        ],
        [
          "OS=='win' and lzf_fault_injection==1",
          {
            "msvs_settings": {
              "VCCLCompilerTool": { "Optimization": "0", "WholeProgramOptimization": "false" },
              "VCLinkerTool": { "LinkTimeCodeGeneration": "0" }
            }
          }
        ],
        [
          "OS=='win' and lzf_fault_injection==0",
          {
            "msvs_settings": {
              "VCCLCompilerTool": {
                "Optimization": "2",  # /O2
                "FavorSizeOrSpeed": 1
              }
            }
          }
        ]
      ]
    }
  ]
}

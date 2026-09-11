{
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
          "OS=='win'",
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

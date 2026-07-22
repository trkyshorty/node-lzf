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
      "conditions": [
        [
          'OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris"',
          {
            "cflags": [ "-O3" ],
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
              "OTHER_CFLAGS": [ "-O3" ]
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

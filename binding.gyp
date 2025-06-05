{
  "targets": [
    {
      "target_name": "lzf",
      "sources": [
        "src/lzf.cc",
        "src/lzf/lzf_c.c",
        "src/lzf/lzf_d.c"
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "src/lzf"
      ],
      "conditions": [
        [
          'OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris"',
          {
            "cflags": [ "-O3" ],
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

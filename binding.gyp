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
      # -Wno-implicit-fallthrough: upstream liblzf (src/lzf/lzf_d.cc) kopyalama döngülerini
      # bilinçli olarak "case" düşüşüyle (Duff's device) açar; gcc'nin -Wextra ile açılan
      # uyarısı burada hata değildir. Satıcı kodu ile kendi kodumuz (src/lzf.cc) tek hedefte
      # derlendiğinden bastırma hedef genelidir; algoritmayı yeniden yazmıyoruz. MSVC
      # bu uyarıyı vermediği için Windows'a bayrak eklenmez.
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

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "simd_ops",
    srcs = [
        "simd_ops.cc",
    ],
    hdrs = [
        "simd_ops.h",
    ],
    copts = ["-O3"],
    deps = [
        "@com_google_highway//:algo",
        "@com_google_highway//:bit_set",
        "@com_google_highway//:dot",
        "@com_google_highway//:hwy",
        "@com_google_highway//:math",
    ],
)

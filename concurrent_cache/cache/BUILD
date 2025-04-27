package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "options",
    hdrs = [
        "options.h",
    ],
    deps = [
        "//concurrent_cache/common:time",
    ],
)

cc_library(
    name = "hashmap",
    # srcs = ["clock_cache_impl.cc"],
    hdrs = [
        "hashmap.h",
        "hashmap_impl.h",
    ],
    deps = [
        "//concurrent_cache/cache/detail:iterator",
        "//concurrent_cache/common:allign",
        "//concurrent_cache/simd:simd_ops",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "cache",
    # srcs = ["clock_cache_impl.cc"],
    hdrs = [
        "cache.h",
        "cache_impl.h",
        "ttl_cache.h",
    ],
    deps = [
        ":options",
        "//concurrent_cache/cache/detail:hazptr",
        "//concurrent_cache/cache/detail:iterator",
        "//concurrent_cache/common:allign",
        "//concurrent_cache/common:time",
        "//concurrent_cache/simd:simd_ops",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "cache_test",
    size = "small",
    srcs = ["cache_test.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lunwind",
        "-ldouble-conversion",
        "-lglog",
        "-lgflags",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    deps = [
        ":cache",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "hashmap_test",
    size = "small",
    srcs = ["hashmap_test.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lunwind",
        "-ldouble-conversion",
        "-lglog",
        "-lgflags",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    deps = [
        ":hashmap",
        "@com_google_googletest//:gtest_main",
    ],
)

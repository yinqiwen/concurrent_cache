package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "allign",
    hdrs = [
        "allign.h",
    ],
)

cc_library(
    name = "time",
    hdrs = [
        "time.h",
    ],
)

cc_library(
    name = "pool",
    hdrs = [
        "pool.h",
    ],
)

cc_library(
    name = "lock",
    srcs = ["lock.cc"],
    hdrs = [
        "lock.h",
    ],
    deps = [":allign"],
)

cc_test(
    name = "lock_test",
    size = "small",
    srcs = ["lock_test.cc"],
    linkopts = [
        "-L/usr/local/lib64",
        "-lfmt",
        "-L/usr/local/lib",
        "-lfolly",
        "-lglog",
        "-lgflags",
        "-ldl",
        "-liberty",
    ],
    linkstatic = True,
    deps = [
        ":lock",
        "@com_google_googletest//:gtest_main",
    ],
)

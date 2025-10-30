add_llvm_library(MyAlwaysInline MODULE
    MyAlwaysInline.cpp

    PLUGIN_TOOL
    opt

    LINK_COMPONENTS
        Core
        IPO
        TransformUtils
)

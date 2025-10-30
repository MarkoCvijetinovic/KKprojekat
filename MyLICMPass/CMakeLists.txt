add_llvm_library( LLVMMyLICMPass MODULE
        MyLICMPass.cpp

        PLUGIN_TOOL
        opt
)
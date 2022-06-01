#include <iostream>
#include <memory>

#include "BasicBlock.h"
#include "Constant.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "IRStmtBuilder.h"
#include "Module.h"
#include "Type.h"

#ifdef DEBUG
#define DEBUG_OUTPUT std::cout << __LINE__ << std::endl;
#else
#define DEBUG_OUTPUT
#endif

#define CONST_INT(num) ConstantInt::get(num, module)

#define CONST_FP(num) ConstantFloat::get(num, module)

int main() {
    auto module = new Module{"assign_test code"};
    auto builder = new IRStmtBuilder{nullptr, module};
    auto Int32Type = Type::get_int32_type(module);
    auto FloatType_ = Type::get_float_type(module);


    auto mainFun = Function::create(FunctionType::get(Int32Type, {}), "main", module);
    auto bb = BasicBlock::create(module, "entry", mainFun);
    builder->set_insert_point(bb);
    
    auto bAlloca = builder->create_alloca(FloatType_);
    builder->create_store(CONST_FP(1.8), bAlloca);

    auto *arrayType_a = ArrayType::get(Int32Type, 2);
    auto aAlloca = builder->create_alloca(arrayType_a);

    auto a0Gep = builder->create_gep(aAlloca, {CONST_INT(0), CONST_INT(0)});
    auto a1Gep = builder->create_gep(aAlloca, {CONST_INT(0), CONST_INT(1)});
    builder->create_store(CONST_INT(2), a0Gep);


    auto a0Load = builder->create_load(a0Gep);
    auto bLoad = builder->create_load(bAlloca);
    auto a0FloatLoad = builder->create_sitofp(a0Load, FloatType_ );
    auto fmul = builder->create_fmul(a0FloatLoad, bLoad);
    auto imul = builder->create_fptosi(fmul, Int32Type);
    builder->create_store(imul, a1Gep);
    builder->create_ret(builder->create_load(a1Gep));



    std::cout << module->print();
    delete module;
    return 0;
}

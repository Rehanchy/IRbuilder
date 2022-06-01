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
    auto module = new Module{"if_test code"};
    auto builder = new IRStmtBuilder{nullptr, module};
    auto Int32Type = Type::get_int32_type(module);

    auto a =
        GlobalVariable::create("a", module, Int32Type, false, CONST_INT(0));

    auto mainFun =
        Function::create(FunctionType::get(Int32Type, {}), "main", module);
    auto bb = BasicBlock::create(module, "entry", mainFun);
    builder->set_insert_point(bb);

    builder->create_store(CONST_INT(10), a);

    auto return_a = BasicBlock::create(module, "return_a", mainFun);
    auto return_0 = BasicBlock::create(module, "return_0", mainFun);

    auto aLoad = builder->create_load(a);
    auto icmp = builder->create_icmp_gt(aLoad, CONST_INT(0));

    builder->create_cond_br(icmp, return_a, return_0);

    builder->set_insert_point(return_a);
    builder->create_ret(builder->create_load(a));

    builder->set_insert_point(return_0);
    builder->create_ret(CONST_INT(0));

    std::cout << module->print();
    delete module;
    return 0;
}

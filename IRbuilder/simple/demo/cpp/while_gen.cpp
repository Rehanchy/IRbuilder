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

#define CONST_FP(num) \
    ConstantFloat::get(num, module)

int main() {
    auto module = new Module("while_test code");
    auto builder = new IRStmtBuilder(nullptr, module);
    Type *Int32Type = Type::get_int32_type(module);

    auto a =
        GlobalVariable::create("a", module, Int32Type, false, CONST_INT(0));
    auto b =
        GlobalVariable::create("b", module, Int32Type, false, CONST_INT(0));

    auto mainFun =
        Function::create(FunctionType::get(Int32Type, {}), "main", module);
    auto bb = BasicBlock::create(module, "entry", mainFun);
    builder->set_insert_point(bb);

    builder->create_store(CONST_INT(0), b);
    builder->create_store(CONST_INT(3), a);

    auto cond = BasicBlock::create(module, "cond", mainFun);
    auto while_true = BasicBlock::create(module, "while_true", mainFun);
    auto ret_stmt = BasicBlock::create(module, "return", mainFun);

    builder->create_br(cond);

    builder->set_insert_point(cond);

    auto aLoad = builder->create_load(a);
    auto icmp = builder->create_icmp_gt(aLoad, CONST_INT(0));

    builder->create_cond_br(icmp, while_true, ret_stmt);

    builder->set_insert_point(while_true);

    auto bLoad = builder->create_load(b);
    aLoad = builder->create_load(a);
    auto add_res = builder->create_iadd(bLoad, aLoad);
    builder->create_store(add_res, b);

    aLoad = builder->create_load(a);
    auto res_sub = builder->create_isub(aLoad, CONST_INT(1));
    builder->create_store(res_sub, a);

    builder->create_br(cond);

    builder->set_insert_point(ret_stmt);
    bLoad = builder->create_load(b);
    builder->create_ret(bLoad);

    std::cout << module->print();
    delete module;
    return 0;
}

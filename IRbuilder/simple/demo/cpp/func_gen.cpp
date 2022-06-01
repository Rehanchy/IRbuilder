#include <iostream>
#include <memory>

#include "BasicBlock.h"
#include "Constant.h"
#include "Function.h"
#include "IRStmtBuilder.h"
#include "Module.h"
#include "Type.h"



#ifdef DEBUG  
#define DEBUG_OUTPUT std::cout << __LINE__ << std::endl;  
#else
#define DEBUG_OUTPUT
#endif

#define CONST_INT(num) \
    ConstantInt::get(num, module)

#define CONST_FP(num) \
    ConstantFloat::get(num, module) 

int main() {
    auto module = new Module("func code");  
    auto builder = new IRStmtBuilder(nullptr, module);
    Type *Int32Type = Type::get_int32_type(module);
    /* function add */
    std::vector<Type *> Ints(2, Int32Type);                     // two args int type
    auto addFunTy = FunctionType::get(Int32Type, Ints);
    auto addFun = Function::create(addFunTy, "add", module);

    auto bb = BasicBlock::create(module, "entry", addFun);

    builder->set_insert_point(bb);

    auto retAlloca = builder->create_alloca(Int32Type);         // alloc space for ret value
    auto aAlloca = builder->create_alloca(Int32Type);           // alloc space for a
    auto bAlloca = builder->create_alloca(Int32Type);           // alloc space for b

    std::vector<Value *> args;                                  // get arguments
    for(auto arg = addFun->arg_begin(); arg != addFun->arg_end(); arg++) {
        args.push_back(*arg);                                   // get element
    }

    builder->create_store(args[0], aAlloca);
    builder->create_store(args[1], bAlloca);                    // store arguments

    
    std::cout << "ok\n";
    auto aLoad = builder->create_load(aAlloca);
    auto bLoad = builder->create_load(bAlloca);                 // get a & b to add

    auto add = builder->create_iadd(aLoad, bLoad);              // a + b
    auto sub = builder->create_isub(add, CONST_INT(1));         // result - 1
    builder->create_store(sub, retAlloca);

    auto retLoad = builder->create_load(retAlloca);
    builder->create_ret(retLoad);                                  
    
    /* main */
    auto mainFun = Function::create(FunctionType::get(Int32Type, {}),
                                    "main", module);
    bb = BasicBlock::create(module, "entry", mainFun);
    builder->set_insert_point(bb);

    retAlloca = builder->create_alloca(Int32Type);
    aAlloca = builder->create_alloca(Int32Type);
    bAlloca = builder->create_alloca(Int32Type);
    auto cAlloca = builder->create_alloca(Int32Type);           // alloc for a, b, c

    builder->create_store(CONST_INT(3), aAlloca);
    builder->create_store(CONST_INT(2), bAlloca);
    builder->create_store(CONST_INT(5), cAlloca);

    aLoad = builder->create_load(aAlloca);
    bLoad = builder->create_load(bAlloca);
    auto cLoad = builder->create_load(cAlloca);                 // get a, b, c

    auto call = builder->create_call(addFun, {aLoad, bLoad});   // func call add
    add = builder->create_iadd(cLoad, call);                    // add_ret + c

    builder->create_store(add, retAlloca);
    retLoad = builder->create_load(retAlloca);
    builder->create_ret(retLoad);                               // ret

    std::cout << module->print();
    delete module;
    return 0;
}

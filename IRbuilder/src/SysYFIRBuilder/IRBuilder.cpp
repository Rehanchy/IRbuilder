#include "IRBuilder.h"
#include "BasicBlock.h"
#include "SyntaxTree.h"
#include "Type.h"

#define CONST_INT(num) ConstantInt::get(num, module.get())
#define CONST_FLOAT(num) ConstantFloat::get(num, module.get())

// You can define global variables and functions here
int initval_depth = 0;
int initval_proceed_stage = 0; // means we are initializing which dimension
//int is_exprsion = 0;
std::vector<int> dimension_vec; // for MultiDimensionArray
std::vector<int> dimension_vec_funcparam;
std::vector<std::vector<float>> dimension_length_vec; // store inital value
std::vector<float> multiDimInitVec; // for initval
// to store state

struct ConstExpr {
    bool is_valid; // 为真代表表达式确实是个可计算的常值
    bool is_int; // 为真代表表达式计算结果是个 int
    int int_value; // 当 is_int 为真时此成员有效
    float float_value; // 当 is_int 为假时此成员有效
} const_expr{false, false, 0, 0.0};

// store temporary value
Value *tmp_val = nullptr;
Value *tmp_addr = nullptr; // 地址
int label = 0;
bool is_func_arg = false;
std::vector<BasicBlock*> tmp_condbb;
std::vector<BasicBlock*> tmp_falsebb;
std::vector<float> array_inital;            // for each dimension store the num
// 这里面保存了所有的全局的 const int 以及 const int 数组变量，std::string 是 它的名字，std::vector 里放它的值
std::map<std::string, std::vector<int>> const_int_var;
// 这里面保存了所有的全局的 const float 以及 const float 数组变量，std::string 是 它的名字，std::vector 里放它的值
std::map<std::string, std::vector<float>> const_float_var;

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *FLOAT_T;
Type *INT32PTR_T;
Type *FLOATPTR_T;
std::map<SyntaxTree::Type,Type *> TypeMap;

void IRBuilder::visit(SyntaxTree::Assembly &node) {
    VOID_T = Type::get_void_type(module.get());
    INT1_T = Type::get_int1_type(module.get());
    INT32_T = Type::get_int32_type(module.get());
    FLOAT_T = Type::get_float_type(module.get());
    INT32PTR_T = Type::get_int32_ptr_type(module.get());
    FLOATPTR_T = Type::get_float_ptr_type(module.get());

    TypeMap[SyntaxTree::Type::VOID]=VOID_T;
    TypeMap[SyntaxTree::Type::BOOL]=INT1_T;
    TypeMap[SyntaxTree::Type::INT]=INT32_T;
    TypeMap[SyntaxTree::Type::FLOAT]=FLOAT_T;

    for (const auto &def : node.global_defs) {
        def->accept(*this);
    }
}

// You need to fill them

void IRBuilder::visit(SyntaxTree::InitVal &node)
{
    // when we are here, the dimension_vec have already be fulfilled
    // that means we could get the length of each dimension, and also dimension depth
    
    if (node.isExp)
    {
        node.expr->accept(*this); // inside expr, this would help get value
                                  // VarDef hold the place for alloca
        if(initval_depth == dimension_vec.size()) // now we get value!!
        {
            if(const_expr.is_int)
                multiDimInitVec.push_back(float(const_expr.int_value));
            else
                multiDimInitVec.push_back(const_expr.float_value);
        }
    }
    else
    {
        for (auto item : node.elementList)
        {
            initval_depth++;        // depth++, now get inside to get value
            item->accept(*this);    // tell this part if next level is Exp or not
            if(initval_depth == dimension_vec.size() && item == node.elementList.back()) // complete one dimension
            {
                dimension_length_vec.push_back(multiDimInitVec);
                initval_proceed_stage++;
                multiDimInitVec.clear(); // for next stage
            }
            initval_depth--;        // wow, I got out, depth--, shall we proceed?
            if(const_expr.is_int == true)
            {
                array_inital.push_back((float)const_expr.int_value);
            }
            else
            {
                array_inital.push_back(const_expr.float_value);
            }
        } // we will wait to see if there needs more data pass
    }
    return;
}

void IRBuilder::visit(SyntaxTree::FuncDef &node) {
    std::vector<Type *> params;
    int count = 0;
    for(const auto &param : node.param_list->params){
        if (param->array_index.empty())
            params.push_back(TypeMap[param->param_type]);
        else{
            std::vector<int> tmp;
            for(auto expr:param->array_index){
                if (expr != nullptr){
                    expr->accept(*this);
                    tmp.push_back(const_expr.int_value);
                }
                count++;
            }
            if(count == 1)
                params.push_back(PointerType::get(TypeMap[param->param_type]));
            else{
                //std::vector<int> tmp(dimension_vec.begin(), dimension_vec.end()-1);
                params.push_back(PointerType::get(MultiDimensionArrayType::get(TypeMap[param->param_type], tmp, count-1)));
            }
        }
    }
    auto FuncType = FunctionType::get(TypeMap[node.ret_type], params);
    auto Func = Function::create(FuncType, node.name, builder->get_module());
    scope.push(node.name,Func);
    auto bb = BasicBlock::create(builder->get_module(), "entry" + std::to_string(label++), Func);
    builder->set_insert_point(bb);
    scope.enter();
    node.param_list->accept(*this);
    for (auto statement : node.body->body) {
        statement->accept(*this);
        if(dynamic_cast<SyntaxTree::ReturnStmt*>(statement.get())){
            break;
        }
    }
    if (builder->get_insert_block()->get_terminator() == nullptr){
        if (node.ret_type == SyntaxTree::Type::VOID)
            builder->create_void_ret();
        else if (node.ret_type == SyntaxTree::Type::FLOAT)
            builder->create_ret(CONST_FLOAT(0.));
        else
            builder->create_ret(CONST_INT(0));
    }
    scope.exit();
}

void IRBuilder::visit(SyntaxTree::FuncFParamList &node) {
    int count = 0;
    auto Argument = builder->get_module()->get_functions().back()->arg_begin();//当前的函数应该是函数表中最后一个函数
    for (const auto &param : node.params){
        param->accept(*this);                                               //访问参数
        if(param->array_index.empty()) {
            auto paramAlloc = builder->create_alloca(TypeMap[param->param_type]);     //分配空间
            builder->create_store(*Argument, paramAlloc);                            //存参数的值
            scope.push(param->name, paramAlloc);                              //加入符号表

        } else {
            for(auto expr:param->array_index)
                count++;
            if(count == 1){
                auto paramAlloc =
                        param->param_type == SyntaxTree::Type::FLOAT
                        ? builder->create_alloca(FLOATPTR_T)
                        : builder->create_alloca(INT32PTR_T);
                builder->create_store(*Argument, paramAlloc);                            //存参数的值
                scope.push(param->name, paramAlloc);                              //加入符号表
            }
            else{
                //std::vector<int> tmp(dimension_vec.begin(), dimension_vec.end()-1);
                auto *MultiarrayType_local = PointerType::get(MultiDimensionArrayType::get(TypeMap[param->param_type], dimension_vec_funcparam, count-1));
                auto multiArrayLocal = builder->create_alloca(MultiarrayType_local);
                builder->create_store(*Argument, multiArrayLocal);
                scope.push(param->name, multiArrayLocal);
                dimension_vec_funcparam.clear();
            }
        }
        Argument++;                                                             //下一个参数值
    }
}

void IRBuilder::visit(SyntaxTree::FuncParam &node) {
    for(auto Exp:node.array_index){                                             //遍历每个Expr
        if (Exp != nullptr){
            Exp->accept(*this);
            dimension_vec_funcparam.push_back(const_expr.int_value);
        }
    }
}

void IRBuilder::visit(SyntaxTree::VarDef &node)
{ // we will need to know glob/local here // seems const is not considered
    /* first judge is int or float */
    Type *Vartype;
    if (node.btype == SyntaxTree::Type::INT)
    {
        Vartype = INT32_T;
    }
    else
    {
        Vartype = FLOAT_T;
    }
    auto zero_initializer = ConstantZero::get(Vartype, module.get());
    int count = 0;
    int DimensionLength = 0;
    for (auto length : node.array_length)
    {
        length->accept(*this);
        count++; // this is about dimension
        DimensionLength = const_expr.int_value; // no checking here a[float] was not permitted // for one dimension array
        dimension_vec.push_back(const_expr.int_value); // for multi dimension array to get length for each dimension
    }
    if (node.is_inited) // initial value exist
    {
        node.initializers->accept(*this); // get the value here
        if (scope.in_global())
        {
            if (count == 0) // this is not an array
            {
                if(!const_expr.is_int && node.btype == SyntaxTree::Type::INT) // float value->int init
                {    
                    auto Adjusted_Inital = ConstantInt::get(int(const_expr.float_value), module.get());
                    if(node.is_constant)
                    {
                        auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, true, Adjusted_Inital);
                        scope.push(node.name, global_init);
                    }
                    else
                    {
                        auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, false, Adjusted_Inital);
                        scope.push(node.name, global_init);
                    }
                    int val = (int)const_expr.float_value;
                    std::vector<int> init_vec;
                    init_vec.push_back(val);
                    const_int_var[node.name] = init_vec;
                }
                else if(const_expr.is_int && node.btype == SyntaxTree::Type::FLOAT)// int value->float init
                {
                    auto Adjusted_Inital = ConstantFloat::get(float(const_expr.int_value), module.get());
                    if(node.is_constant)
                    {
                        auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, true, Adjusted_Inital);
                        scope.push(node.name, global_init);
                    }
                    else
                    {
                        auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, false, Adjusted_Inital);
                        scope.push(node.name, global_init);
                    }
                    float val = (float)const_expr.int_value;
                    std::vector<float> init_vec;
                    init_vec.push_back(val);
                    const_float_var[node.name] = init_vec;
                }
                else
                {
                    if(node.btype == SyntaxTree::Type::FLOAT)
                    {
                        auto Adjusted_Inital = ConstantFloat::get(const_expr.float_value, module.get());
                        if(node.is_constant)
                        {
                            auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, true, Adjusted_Inital);
                            scope.push(node.name, global_init);
                        }
                        else
                        {
                            auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, false, Adjusted_Inital);
                            scope.push(node.name, global_init);
                        }
                    }
                    else
                    {
                        auto Adjusted_Inital = ConstantInt::get(const_expr.int_value, module.get());
                        if(node.is_constant)
                        {
                            auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, true, Adjusted_Inital);
                            scope.push(node.name, global_init);
                        }
                        else
                        {
                            auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, false, Adjusted_Inital);
                            scope.push(node.name, global_init);
                        }
                    }
                    if(const_expr.is_int)
                    {
                        int val = const_expr.int_value;
                        std::vector<int> init_vec;
                        init_vec.push_back(val);
                        const_int_var[node.name] = init_vec;
                    }
                    else
                    {
                        float val = const_expr.float_value;
                        std::vector<float> init_vec;
                        init_vec.push_back(val);
                        const_float_var[node.name] = init_vec;
                    } // add to const global vector for calculate use
                }
            }
            else if(count == 1)   // array one dimension
            {
                auto *arrayType_global = ArrayType::get(Vartype, DimensionLength);
                if(node.btype == SyntaxTree::Type::FLOAT)
                {
                    std::vector<Constant *> init_val;
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size()) // still have value
                            init_val.push_back(CONST_FLOAT(array_inital[i])); // float array
                        else
                            init_val.push_back(CONST_FLOAT(0.0));
                    }
                    auto arrayInitializer = ConstantArray::get(arrayType_global, init_val);
                    if(node.is_constant)
                    {
                        auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, true, arrayInitializer);
                        scope.push(node.name, arrayGlobal);
                    }
                    else
                    {
                        auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, false, arrayInitializer);
                        scope.push(node.name, arrayGlobal);
                    }
                }
                else    // INT
                {
                    std::vector<Constant *> init_val;
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size()) // still have value
                            init_val.push_back(CONST_INT((int)array_inital[i])); // float array
                        else
                            init_val.push_back(CONST_INT(0));
                    }
                    auto arrayInitializer = ConstantArray::get(arrayType_global, init_val);
                    if(node.is_constant)
                    {
                        auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, true, arrayInitializer);
                        scope.push(node.name, arrayGlobal);
                    }
                    else
                    {
                        auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, false, arrayInitializer);
                        scope.push(node.name, arrayGlobal);
                    }
                }
                // it's for the const vector use
                if(node.btype == SyntaxTree::Type::FLOAT)
                {
                    std::vector<float> init_vec;
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size())
                            init_vec.push_back(array_inital[i]);
                        else
                            init_vec.push_back(0.0);
                    }
                    const_float_var[node.name] = init_vec;
                }
                else
                {
                    std::vector<int> init_vec;
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size())
                            init_vec.push_back((int)array_inital[i]);
                        else
                            init_vec.push_back(0);
                    }
                    const_int_var[node.name] = init_vec;
                }
                array_inital.clear(); // for next def
            }
            else // multi dimension array
            {
                auto *multiArrayType_global = MultiDimensionArrayType::get(Vartype, dimension_vec, count);
                //scope.push(node.name, multiArrayLocal);
                if(node.btype == SyntaxTree::Type::FLOAT)
                {                   
                    int i = 0;
                    int ResultSize = 1;
                    while(i < dimension_vec.size())
                    {
                        ResultSize = ResultSize * dimension_vec[i];
                        i++;
                    }
                    std::vector<std::vector<Constant*>> multi_init_vec;
                    int pos = dimension_length_vec.size();
                    for(i = 0; i < pos; i++)
                    {
                        auto temp_vec = dimension_length_vec[i];
                        std::vector<Constant*> init_vec;
                        int inner_pos = dimension_vec.back();
                        for(int j = 0; j < inner_pos; j++)
                        {
                            if(j >= temp_vec.size())
                            {
                                init_vec.push_back(CONST_FLOAT(0.0));
                                j++;
                                continue;
                            }
                            auto t = CONST_FLOAT(temp_vec[j]);
                            init_vec.push_back(t);
                        }
                        multi_init_vec.push_back(init_vec);
                    }
                    auto MultiArrayInitializer = ConstantMultiArray::get(multiArrayType_global, dimension_vec, multi_init_vec, ResultSize);
                    if(node.is_constant)
                    {
                        auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, true, MultiArrayInitializer);
                        scope.push(node.name, multiArrayGlobal);
                    }
                    else
                    {
                        auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, false, MultiArrayInitializer);
                        scope.push(node.name, multiArrayGlobal);
                    }
                }
                else
                {
                    int i = 0;
                    int ResultSize = 1;
                    while(i < dimension_vec.size())
                    {
                        ResultSize = ResultSize * dimension_vec[i];
                        i++;
                    }
                    std::vector<std::vector<Constant*>> multi_init_vec;
                    int pos = dimension_length_vec.size();
                    for(i = 0; i < pos; i++)
                    {
                        auto temp_vec = dimension_length_vec[i];
                        std::vector<Constant*> init_vec;
                        int inner_pos = dimension_vec.back();
                        for(int j = 0; j < inner_pos; j++)
                        {
                            if(j >= temp_vec.size())
                            {
                                init_vec.push_back(CONST_INT(0));
                                j++;
                                continue;
                            }
                            auto t = CONST_INT((int)temp_vec[j]);
                            init_vec.push_back(t);
                        }
                        multi_init_vec.push_back(init_vec);
                    }
                    auto MultiArrayInitializer = ConstantMultiArray::get(multiArrayType_global, dimension_vec, multi_init_vec, ResultSize);
                    if(node.is_constant)
                    {
                        auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, true, MultiArrayInitializer);
                        scope.push(node.name, multiArrayGlobal);
                    }
                    else
                    {
                        auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, false, MultiArrayInitializer);
                        scope.push(node.name, multiArrayGlobal);
                    }  
                }
                // it's for the const vector use
                if(node.btype == SyntaxTree::Type::FLOAT)
                {
                    std::vector<float> init_vec;
                    int dimSize = dimension_vec.size();
                    for(int i = 0; i < dimSize; i++)
                    {
                        auto temp_vec = dimension_length_vec[i];
                        int length = dimension_vec[i];
                        for(int j = 0; j < length; j++)
                        {
                            if(j < temp_vec.size())
                                init_vec.push_back(temp_vec[j]);
                            else
                                init_vec.push_back(0.0);
                        }
                    }
                    const_float_var[node.name] = init_vec;
                }
                else
                {
                    std::vector<int> init_vec;
                    int dimSize = dimension_vec.size();
                    for(int i = 0; i < dimSize; i++)
                    {
                        auto temp_vec = dimension_length_vec[i];
                        int length = dimension_vec[i];
                        for(int j = 0; j < length; j++)
                        {
                            if(j < temp_vec.size())
                                init_vec.push_back((int)temp_vec[j]);
                            else
                                init_vec.push_back(0);
                        }
                    }
                    const_int_var[node.name] = init_vec;
                }
            }
        }
        else // initialed not global
        {
            if (count == 0) // this is not an array
            {
                auto local_init = builder->create_alloca(Vartype);
                scope.push(node.name, local_init);
                if(tmp_val->get_type()->is_float_type() && node.btype == SyntaxTree::Type::INT) // float init
                {
                    auto Adjusted_Inital = builder->create_fptosi(tmp_val, INT32_T);
                    builder->create_store(Adjusted_Inital, local_init);
                }
                else if(tmp_val->get_type()->is_integer_type() && node.btype == SyntaxTree::Type::FLOAT)
                {
                    auto Adjusted_Inital = builder->create_sitofp(tmp_val, FLOAT_T);
                    builder->create_store(Adjusted_Inital, local_init);
                }
                else
                {
                    builder->create_store(tmp_val, local_init);
                }
            }
            else if(count == 1) // one dimension array
            {
                auto *arrayType_local = ArrayType::get(Vartype, DimensionLength);
                auto arrayLocal = builder->create_alloca(arrayType_local);
                if(node.btype == SyntaxTree::Type::FLOAT)
                {
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size()) // still have value
                        {
                            auto Gep = builder->create_gep(arrayLocal, {CONST_INT(0), CONST_INT(i)});
                            builder->create_store(CONST_FLOAT(array_inital[i]), Gep);
                        }
                    }
                    scope.push(node.name, arrayLocal);
                }
                else
                {
                    for(int i = 0; i < DimensionLength; i++)
                    {
                        if(i < array_inital.size()) // still have value
                        {
                            auto Gep = builder->create_gep(arrayLocal, {CONST_INT(0), CONST_INT(i)});
                            builder->create_store(CONST_INT((int)array_inital[i]), Gep);
                        }
                    }
                    scope.push(node.name, arrayLocal);
                }
                array_inital.clear(); // for next def
            }
            else // multi dimension
            {
                auto *multiArrayType_local = MultiDimensionArrayType::get(Vartype, dimension_vec, count);
                auto multiArrayLocal = builder->create_alloca(multiArrayType_local);
                scope.push(node.name, multiArrayLocal);
                //auto tempname = multiArrayLocal->get_name();
                if(node.btype == SyntaxTree::Type::FLOAT)
                {                   
                    int length_sum = 1;
                    for(int i = 0; i < count-1; i++)
                        length_sum *= dimension_vec[i]; // dimension calculation
                    int flag = dimension_length_vec.size();
                    for(int len = 1; len <= length_sum; len++)
                    {   
                        int j = 1;
                        std::vector <int> temp_value_vec;
                        int temp_len = len-1;
                        while(temp_len != 0)
                        {
                            int temp = temp_len % dimension_vec[count-j-1];
                            temp_value_vec.push_back(temp);
                            temp_len = temp_len/dimension_vec[count-j-1];
                            j++;
                            if(count-j-1 < 0)
                                break; // avoid error
                        }
                        auto temp_count = count-1;
                        std::vector<float> temp_init_vec = dimension_length_vec[len-1]; // get initial value vec
                        std::vector<Value *> value_vec; // get ptr
                        int fulfill_need = count - temp_value_vec.size();
                        while(fulfill_need != 0)
                        {
                            auto t = CONST_INT(0);
                            value_vec.push_back(t);
                            fulfill_need--;
                        }
                        while(temp_value_vec.size() != 0)
                        {
                            auto t = CONST_INT(temp_value_vec.back());
                            temp_value_vec.pop_back();
                            value_vec.push_back(t);
                        }
                        for(int k = 0; k < dimension_vec[temp_count]; k++)
                        {
                            value_vec.push_back(CONST_INT(k));
                            auto Gep = builder->create_gep(multiArrayLocal, value_vec);// possible solution selfmade api
                            if(k < temp_init_vec.size())
                            {
                                int key = k;
                                builder->create_store(CONST_FLOAT(temp_init_vec[key]), Gep);
                            }
                            else
                            {
                                builder->create_store(CONST_FLOAT(0.0), Gep);
                            }
                            value_vec.pop_back();
                        }
                        temp_value_vec.clear();
                        temp_init_vec.clear();
                        value_vec.clear();
                    }  
                }
                else
                {
                    int length_sum = 1;
                    for(int i = 0; i < count-1; i++)
                        length_sum *= dimension_vec[i]; // dimension calculation
                    for(int len = 1; len <= length_sum; len++)
                    {   
                        int j = 1;
                        std::vector <int> temp_value_vec;
                        int temp_len = len-1;
                        while(temp_len != 0)
                        {
                            int temp = temp_len % dimension_vec[count-1-j];
                            temp_value_vec.push_back(temp);  // reverse it to get right value
                            temp_len = temp_len/dimension_vec[count-j-1];
                            j++;
                            if(count-j-1 < 0)
                                break; // avoid error
                        }
                        auto temp_count = count-1;
                        std::vector<float> temp_init_vec = dimension_length_vec[len-1]; // get initial value vec
                        std::vector<Value *> value_vec; // get ptr
                        int fulfill_need = count - temp_value_vec.size();
                        while(fulfill_need != 0)
                        {
                            auto t = CONST_INT(0);
                            value_vec.push_back(t);
                            fulfill_need--;
                        }
                        while(temp_value_vec.size() != 0)
                        {
                            auto t = CONST_INT(temp_value_vec.back());
                            temp_value_vec.pop_back();
                            value_vec.push_back(t);
                        }
                        for(int k = 0; k < dimension_vec[temp_count]; k++)
                        {
                            value_vec.push_back(CONST_INT(k));
                            auto Gep = builder->create_gep(multiArrayLocal, value_vec);// possible solution selfmade api
                            if(k < temp_init_vec.size())
                            {
                                int key = k;
                                builder->create_store(CONST_INT((int)temp_init_vec[key]), Gep);
                            }
                            else
                            {
                                builder->create_store(CONST_INT(0), Gep);
                            }
                            value_vec.pop_back();
                        }
                        temp_value_vec.clear();
                        temp_init_vec.clear();
                        value_vec.clear();
                    }  
                }
            }
        }
    }
    else // no need to check type // not initialed
    {
        if (scope.in_global())
        {
            if (count == 0) // this is not an array
            {
                if(node.is_constant) // const or not
                {
                    auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, true, zero_initializer);
                    scope.push(node.name, global_init);
                    if (global_init->get_type()->get_pointer_element_type() == INT32_T)
                    const_int_var[node.name] = std::vector<int>{0};
                    else 
                    const_float_var[node.name] = std::vector<float>{0.0};
                }
                else
                {
                    auto global_init = GlobalVariable::create(node.name, module.get(), Vartype, false, zero_initializer);
                    scope.push(node.name, global_init);
                    if (global_init->get_type()->get_pointer_element_type() == INT32_T)
                    const_int_var[node.name] = std::vector<int>{0};
                    else 
                    const_float_var[node.name] = std::vector<float>{0.0};
                }
            }
            else if(count == 1) // one dimension array
            {
                auto *arrayType_global = ArrayType::get(Vartype, DimensionLength);
                if (arrayType_global->get_array_element_type() == INT32_T)
                    const_int_var[node.name] = std::vector<int>(DimensionLength, 0);
                else
                    const_float_var[node.name] = std::vector<float>(DimensionLength, 0.0);
                if(node.is_constant)
                {
                    auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, true, zero_initializer);
                    scope.push(node.name, arrayGlobal);
                }
                else
                {
                    auto arrayGlobal = GlobalVariable::create(node.name, module.get(), arrayType_global, false, zero_initializer);
                    scope.push(node.name, arrayGlobal);
                }
            }
            else // multi dimension array
            {
                auto *multiArrayType_global = MultiDimensionArrayType::get(Vartype, dimension_vec, count);
                if(node.is_constant)
                {
                    auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, true, zero_initializer);
                    scope.push(node.name, multiArrayGlobal);
                }
                else
                {
                    auto multiArrayGlobal = GlobalVariable::create(node.name, module.get(), multiArrayType_global, false, zero_initializer);
                    scope.push(node.name, multiArrayGlobal);
                }
                dimension_vec.clear();
            }
        } 
        else // not initialed not global
        {
            if (count == 0) // this is not an array
            {
                auto local_init = builder->create_alloca(Vartype);
                scope.push(node.name, local_init);
            }
            else if(count == 1)
            {
                auto *arrayType_local = ArrayType::get(Vartype, DimensionLength);
                auto arrayLocal = builder->create_alloca(arrayType_local);
                scope.push(node.name, arrayLocal);
            }
            else // MultiDimension 
            { 
                auto *multiArrayType_local = MultiDimensionArrayType::get(Vartype, dimension_vec, count);
                auto multiArrayLocal = builder->create_alloca(multiArrayType_local);
                scope.push(node.name, multiArrayLocal);
                dimension_vec.clear();
            }
        }
    }
    initval_depth = 0;
    initval_proceed_stage = 0;
    multiDimInitVec.clear();
    array_inital.clear(); 
    dimension_vec.clear();
    dimension_length_vec.clear(); // needed procedure
}

void IRBuilder::visit(SyntaxTree::LVal &node) {
    auto ret = this->scope.find(node.name, false); // 根据名字获取值
    if (!node.array_index.empty()) {               // 如果是个数组
        std::vector<Value *> idxs{CONST_INT(0)};
        std::vector<int> index_vec;
        for (const auto &expr : node.array_index) { // 计算下标表达式的值
            expr->accept(*this);
            idxs.push_back(tmp_val);
            index_vec.push_back(const_expr.int_value);
        }
        auto len = idxs.size() - 1;
        if (scope.in_global()) {
            const_expr.is_valid = true;
            const_expr.is_int = ret->get_type()->get_pointer_element_type()->get_array_element_type() == INT32_T;
            if (const_expr.is_int) {
                auto get = const_int_var.find(node.name);
                if (ret->get_type()->get_pointer_element_type()->is_array_type()) // 一维数组
                    const_expr.int_value = get->second[index_vec[0]];
                else { // 多维数组
                    auto tmp = static_cast<MultiDimensionArrayType *>(ret->get_type()->get_pointer_element_type());
                    auto dim_vec = tmp->get_dim_vec();
                    auto index = index_vec[0];
                    for (auto i = 1u; i < dim_vec.size(); i++)
                        index = index * dim_vec[i] + index_vec[i];
                    const_expr.int_value = get->second[index];
                }
            } else { // 多维数组
                auto get = const_float_var.find(node.name);
                if (ret->get_type()->get_pointer_element_type()->is_array_type()) // 一维数组
                    const_expr.float_value = get->second[index_vec[0]];
                else {
                    auto tmp = static_cast<MultiDimensionArrayType *>(ret->get_type()->get_pointer_element_type());
                    auto dim_vec = tmp->get_dim_vec();
                    auto index = index_vec[0];
                    for (auto i = 1u; i < dim_vec.size(); i++)
                        index = index * dim_vec[i] + index_vec[i];
                    const_expr.float_value = get->second[index];
                }
            }
        } else {
            if (ret->get_type()->get_pointer_element_type()->is_pointer_type()) {
                ret = this->builder->create_gep(ret, {CONST_INT(0)});
                ret = this->builder->create_load(ret);
                if (ret->get_type()->get_pointer_element_type()->is_multi_array_type()) {
                    const auto tmp = static_cast<MultiDimensionArrayType *>(ret->get_type()->get_pointer_element_type());
                    if (len - 1 != tmp->get_num_of_dimension()) {
                        idxs.push_back(CONST_INT(0));
                    }
                    idxs.erase(idxs.begin());
                    tmp_addr = this->builder->create_gep(ret, std::move(idxs));
                    return;
                }
                idxs.erase(idxs.begin());
                ret = this->builder->create_gep(ret, std::move(idxs)); 
            } else {
                if (ret->get_type()->get_pointer_element_type()->is_multi_array_type()) {
                    const auto tmp = static_cast<MultiDimensionArrayType *>(ret->get_type()->get_pointer_element_type());
                    if (len != tmp->get_num_of_dimension()) {
                        idxs.push_back(CONST_INT(0));
                        tmp_addr = this->builder->create_gep(ret, std::move(idxs));
                        return;
                    }
                }
                ret = this->builder->create_gep(ret, std::move(idxs)); // 获取数组元素
            }
            tmp_addr = ret;
            tmp_val = this->builder->create_load(tmp_addr);
            const_expr.is_valid = false;
        }
        return;
    }
    if (scope.in_global()) {
        const_expr.is_valid = true;
        const_expr.is_int = ret->get_type()->get_pointer_element_type() == INT32_T;
        if (const_expr.is_int) {
            auto get = const_int_var.find(node.name);
            const_expr.int_value = get->second[0];
        } else {
            auto get = const_float_var.find(node.name);
            const_expr.float_value = get->second[0];
        }
    } else {
        if (ret->get_type()->get_pointer_element_type()->is_pointer_type()) {
            tmp_addr = this->builder->create_gep(ret, {CONST_INT(0)});
            tmp_addr = this->builder->create_load(tmp_addr);
            tmp_val = this->builder->create_load(tmp_addr);
        } else if(ret->get_type()->get_pointer_element_type()->is_array_type()
                || ret->get_type()->get_pointer_element_type()->is_multi_array_type())  {
            tmp_addr = this->builder->create_gep(ret, {CONST_INT(0), CONST_INT(0)});
            tmp_val = this->builder->create_load(ret);
        } else {
            tmp_addr = ret;
            tmp_val = this->builder->create_load(ret);
        }
        const_expr.is_valid = false;
    }
}

void IRBuilder::visit(SyntaxTree::AssignStmt &node) {
    node.value->accept(*this);
    auto src = tmp_val; // 获取等号右边表达式的值
    node.target->accept(*this);
    auto dest = tmp_addr; // 获取左值
    if (dest->get_type()->get_pointer_element_type() == FLOAT_T) {
        if (src->get_type() == INT32_T)
            src = this->builder->create_sitofp(src, FLOAT_T);
        if (src->get_type() == INT1_T) {
            src = this->builder->create_zext(src, INT32_T);
            src = this->builder->create_sitofp(src, FLOAT_T);
        }
    }
    if (dest->get_type()->get_pointer_element_type() == INT32_T) {
        if (src->get_type() == FLOAT_T)
            src = this->builder->create_fptosi(src, INT32_T);
        if (src->get_type() == INT1_T)
            src = this->builder->create_zext(src, INT32_T);
    }
    this->builder->create_store(src, dest); // 存储值
    tmp_val = src; // 整个表达式的值就是等号右边的表达式的值
}

void IRBuilder::visit(SyntaxTree::Literal &node) {
    if (node.literal_type == SyntaxTree::Type::INT) { // 字面量是个整形
        tmp_val = CONST_INT(node.int_const);
        const_expr = {true, true, node.int_const, 0.0};
    } else {// 字面量是个浮点数
        tmp_val = CONST_FLOAT(node.float_const);
        const_expr = {true, false, 0, (float)node.float_const};
    }
}

void IRBuilder::visit(SyntaxTree::ReturnStmt &node) {
    if (node.ret.get() == nullptr) { // 返回值为 void
        this->builder->create_void_ret();
        tmp_val = nullptr;
        return;
    }
    node.ret->accept(*this);
    auto retType = this->module->get_functions().back()->get_return_type();
    if (retType == FLOAT_T) {
        if (tmp_val->get_type() == INT32_T)
            tmp_val = this->builder->create_sitofp(tmp_val, FLOAT_T);
        if (tmp_val->get_type() == INT1_T) {
            tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_sitofp(tmp_val, FLOAT_T);
        }
    }
    if (retType == INT32_T) {
        if (tmp_val->get_type() == FLOAT_T)
            tmp_val = this->builder->create_fptosi(tmp_val, INT32_T);
        if (tmp_val->get_type() == INT1_T)
            tmp_val = this->builder->create_zext(tmp_val, INT32_T);
    }
    this->builder->create_ret(tmp_val);
    tmp_val = nullptr; // return 语句没有值
}

void IRBuilder::visit(SyntaxTree::BlockStmt &node) {
    this->scope.enter(); // 进入一个新的块作用域
    for (const auto &stmt : node.body) {
        stmt->accept(*this); // 对块里每条语句都生成代码
        if (dynamic_cast<SyntaxTree::ReturnStmt *>(stmt.get())) // 遇到返回语句，以后的语句不用生成代码了
            break;
    }
    tmp_val = nullptr;
    this->scope.exit(); // 退出块作用域
}

void IRBuilder::visit(SyntaxTree::EmptyStmt &node) {}

void IRBuilder::visit(SyntaxTree::ExprStmt &node) {
    node.exp->accept(*this);
    // 表达式的值就是 exp 的值，无需改变 tmp_val
}

void IRBuilder::visit(SyntaxTree::UnaryCondExpr &node) {
    node.rhs->accept(*this);
    if (scope.in_global()) {
        if (const_expr.is_valid) {
            if (const_expr.is_int)
                const_expr.int_value = const_expr.int_value == 0;
            else {
                const_expr.is_int = true;
                const_expr.int_value = const_expr.float_value == 0.0;
            }
        }
    } else {
        auto rhs = tmp_val;
        if (tmp_val->get_type()->is_float_type())
            tmp_val = this->builder->create_fcmp_eq(rhs, CONST_FLOAT(0));
        else {
            if (tmp_val->get_type() == INT1_T)
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_fcmp_eq(rhs, CONST_INT(0));
        }
        // 如果 rhs 为 0，那 tmp_val = 1 否则 tmp_val = 0
        const_expr.is_valid = false;
    }
}

void IRBuilder::visit(SyntaxTree::BinaryCondExpr &node) {
    if (node.op == SyntaxTree::BinaryCondOp::LOR) {
        node.lhs->accept(*this);
        auto lhs_const = const_expr;
        const_expr.is_valid = false;

        auto if_true = BasicBlock::create( // 短路计算
                this->module.get(),
                "if_true" + std::to_string(label++),
                this->builder->get_insert_block()->get_parent());
        auto if_false = BasicBlock::create(
                this->module.get(),
                "if_false" + std::to_string(label++),
                this->builder->get_insert_block()->get_parent());

        if (tmp_val->get_type()->is_float_type()) // 为真则不用算右边
            tmp_val = this->builder->create_fcmp_ne(tmp_val, CONST_FLOAT(0.0));
        else {
            if (tmp_val->get_type() == INT1_T)
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_icmp_ne(tmp_val, CONST_INT(0));
        }

        Value *ret = this->builder->create_zext(tmp_val, INT32_T);
        auto retAlloca = this->builder->create_alloca(INT32_T); // 保存表达式比较结果
        this->builder->create_store(ret, retAlloca);

        this->builder->create_cond_br(tmp_val, if_true, if_false);
        this->builder->set_insert_point(if_false);

        node.rhs->accept(*this);
        auto rhs_const = const_expr;
        if (tmp_val->get_type()->is_float_type())
            tmp_val = this->builder->create_fcmp_ne(tmp_val, CONST_FLOAT(0.0));
        else {
            if (tmp_val->get_type() == INT1_T)
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_icmp_ne(tmp_val, CONST_INT(0));
        }

        ret = this->builder->create_load(retAlloca);
        tmp_val = this->builder->create_zext(tmp_val, INT32_T);
        tmp_val = this->builder->create_iadd(ret, tmp_val); // 把两个逻辑表达式的结果加在一起
        this->builder->create_store(tmp_val, retAlloca);    // 结果存到 retAlloca
        this->builder->create_br(if_true);

        this->builder->set_insert_point(if_true); // 左式为真，启动短路计算
        ret = this->builder->create_load(retAlloca);
        tmp_val = this->builder->create_icmp_gt(ret, CONST_INT(0)); // 只要结果不是 0 就代表有一个为 1

        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            lhs_const.int_value = lhs_const.is_int
                                  ? lhs_const.int_value != 0
                                  : lhs_const.float_value != 0.0;
            rhs_const.int_value = rhs_const.is_int
                                  ? rhs_const.int_value != 0
                                  : rhs_const.float_value != 0.0;
            const_expr.int_value = lhs_const.int_value || rhs_const.int_value;
        } else {
            const_expr.is_valid = false;
        }
        return;
    }
    if (node.op == SyntaxTree::BinaryCondOp::LAND) {
        node.lhs->accept(*this);
        auto lhs_const = const_expr;
        const_expr.is_valid = false;

        auto if_true = BasicBlock::create( // 短路计算
                this->module.get(),
                "if_true" + std::to_string(label++),
                this->builder->get_insert_block()->get_parent());
        auto if_false = BasicBlock::create(
                this->module.get(),
                "if_false" + std::to_string(label++),
                this->builder->get_insert_block()->get_parent());

        if (tmp_val->get_type()->is_float_type()) // 为假则不用算右边
            tmp_val = this->builder->create_fcmp_ne(tmp_val, CONST_FLOAT(0.0));
        else {
            if (tmp_val->get_type() == INT1_T)
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_icmp_ne(tmp_val, CONST_INT(0));
        }

        Value *ret = this->builder->create_zext(tmp_val, INT32_T);
        auto retAlloca = this->builder->create_alloca(INT32_T);
        this->builder->create_store(ret, retAlloca);

        this->builder->create_cond_br(tmp_val, if_true, if_false);
        this->builder->set_insert_point(if_true);

        node.rhs->accept(*this);
        auto rhs_const = const_expr;
        if (tmp_val->get_type()->is_float_type())
            tmp_val = this->builder->create_fcmp_ne(tmp_val, CONST_FLOAT(0.0));
        else {
            if (tmp_val->get_type() == INT1_T)
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            tmp_val = this->builder->create_icmp_ne(tmp_val, CONST_INT(0));
        }
        ret = this->builder->create_load(retAlloca);
        tmp_val = this->builder->create_zext(tmp_val, INT32_T);
        tmp_val = this->builder->create_iadd(ret, tmp_val); // 把两个表达式结果加一起
        this->builder->create_store(tmp_val, retAlloca);
        this->builder->create_br(if_false);

        this->builder->set_insert_point(if_false); // 左式为假，启动短路计算
        ret = this->builder->create_load(retAlloca);
        tmp_val = this->builder->create_icmp_eq(ret, CONST_INT(2)); // 只要结果等于 2 那就是真
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            lhs_const.int_value = lhs_const.is_int
                                  ? lhs_const.int_value != 0
                                  : lhs_const.float_value != 0.0;
            rhs_const.int_value = rhs_const.is_int
                                  ? rhs_const.int_value != 0
                                  : rhs_const.float_value != 0.0;
            const_expr.int_value = lhs_const.int_value && rhs_const.int_value;
        } else {
            const_expr.is_valid = false;
        }
        return;
    }

    node.lhs->accept(*this);
    auto lhs = tmp_val;
    auto lhs_const = const_expr;
    const_expr.is_valid = false;
    node.rhs->accept(*this);
    auto rhs = tmp_val;
    auto rhs_const = const_expr;

    if (node.op == SyntaxTree::BinaryCondOp::EQ) {
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T); // 类型提升
            tmp_val = this->builder->create_fcmp_eq(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T); // 类型提升
            tmp_val = this->builder->create_fcmp_eq(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_eq(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value == rhs_const.int_value
                                       : lhs_const.int_value == rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value == rhs_const.int_value
                                       : lhs_const.float_value == rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinaryCondOp::GT) { // 以下基本重复，应该可以使用函数指针优化
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_gt(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_gt(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_gt(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value > rhs_const.int_value
                                       : lhs_const.int_value > rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value > rhs_const.int_value
                                       : lhs_const.float_value > rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinaryCondOp::GTE) {
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_ge(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_ge(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_ge(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value >= rhs_const.int_value
                                       : lhs_const.int_value >= rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value >= rhs_const.int_value
                                       : lhs_const.float_value >= rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinaryCondOp::NEQ) {
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_ne(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_ne(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_ne(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value != rhs_const.int_value
                                       : lhs_const.int_value != rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value != rhs_const.int_value
                                       : lhs_const.float_value != rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinaryCondOp::LT) {
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_lt(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_lt(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_lt(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value < rhs_const.int_value
                                       : lhs_const.int_value < rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value < rhs_const.int_value
                                       : lhs_const.float_value < rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinaryCondOp::LTE) {
        if (lhs->get_type()->is_float_type()) {
            if (rhs->get_type()->is_integer_type())
                rhs = this->builder->create_sitofp(rhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_le(lhs, rhs);
        } else if (rhs->get_type()->is_float_type()) {
            lhs = this->builder->create_sitofp(lhs, FLOAT_T);
            tmp_val = this->builder->create_fcmp_le(lhs, rhs);
        } else {
            tmp_val = this->builder->create_icmp_le(lhs, rhs);
        }
        if (lhs_const.is_valid && rhs_const.is_valid) {
            const_expr.is_valid = true;
            const_expr.is_int = true;
            if (lhs_const.is_int) {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.int_value <= rhs_const.int_value
                                       : lhs_const.int_value <= rhs_const.float_value;
            } else {
                const_expr.int_value = rhs_const.is_int
                                       ? lhs_const.float_value <= rhs_const.int_value
                                       : lhs_const.float_value <= rhs_const.float_value;
            }
        } else {
            const_expr.is_valid = false;
        }
    }
}

void IRBuilder::visit(SyntaxTree::BinaryExpr &node) {
    node.lhs->accept(*this);
    auto lhs = tmp_val;
    auto lhs_const = const_expr;
    const_expr.is_valid = false;
    node.rhs->accept(*this);
    auto rhs = tmp_val;
    auto rhs_const = const_expr;

    if (node.op == SyntaxTree::BinOp::PLUS) {
        if (scope.in_global()) {
            if (lhs_const.is_valid && rhs_const.is_valid) {
                const_expr.is_valid = true;
                if (lhs_const.is_int) {
                    if (rhs_const.is_int) {
                        const_expr.is_int = true;
                        const_expr.int_value = lhs_const.int_value + rhs_const.int_value;
                    } else {
                        const_expr.is_int = false;
                        const_expr.float_value = lhs_const.int_value + rhs_const.float_value;
                    }
                } else {
                    const_expr.is_int = false;
                    const_expr.float_value = rhs_const.is_int
                                             ? lhs_const.float_value + rhs_const.int_value
                                             : lhs_const.float_value + rhs_const.float_value;
                }
            } else {
                const_expr.is_valid = false;
            }
        } else {
            if (lhs->get_type()->is_float_type()) {
                if (rhs->get_type()->is_integer_type())
                    rhs = this->builder->create_sitofp(rhs, FLOAT_T);
                tmp_val = this->builder->create_fadd(lhs, rhs);
            } else if (rhs->get_type()->is_float_type()) {
                lhs = this->builder->create_sitofp(lhs, FLOAT_T);
                tmp_val = this->builder->create_fadd(lhs, rhs);
            } else {
                tmp_val = this->builder->create_iadd(lhs, rhs);
            }
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinOp::MINUS) {
        if (scope.in_global()) {
            if (lhs_const.is_valid && rhs_const.is_valid) {
                const_expr.is_valid = true;
                if (lhs_const.is_int) {
                    if (rhs_const.is_int) {
                        const_expr.is_int = true;
                        const_expr.int_value = lhs_const.int_value - rhs_const.int_value;
                    } else {
                        const_expr.is_int = false;
                        const_expr.float_value = lhs_const.int_value - rhs_const.float_value;
                    }
                } else {
                    const_expr.is_int = false;
                    const_expr.float_value = rhs_const.is_int
                                             ? lhs_const.float_value - rhs_const.int_value
                                             : lhs_const.float_value - rhs_const.float_value;
                }
            } else {
                const_expr.is_valid = false;
            }
        } else {
            if (lhs->get_type()->is_float_type()) {
                if (rhs->get_type()->is_integer_type())
                    rhs = this->builder->create_sitofp(rhs, FLOAT_T);
                tmp_val = this->builder->create_fsub(lhs, rhs);
            } else if (rhs->get_type()->is_float_type()) {
                lhs = this->builder->create_sitofp(lhs, FLOAT_T);
                tmp_val = this->builder->create_fsub(lhs, rhs);
            } else {
                tmp_val = this->builder->create_isub(lhs, rhs);
            }
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinOp::MULTIPLY) {
        if (scope.in_global()) {
            if (lhs_const.is_valid && rhs_const.is_valid) {
                const_expr.is_valid = true;
                if (lhs_const.is_int) {
                    if (rhs_const.is_int) {
                        const_expr.is_int = true;
                        const_expr.int_value = lhs_const.int_value * rhs_const.int_value;
                    } else {
                        const_expr.is_int = false;
                        const_expr.float_value = lhs_const.int_value * rhs_const.float_value;
                    }
                } else {
                    const_expr.is_int = false;
                    const_expr.float_value = rhs_const.is_int
                                             ? lhs_const.float_value * rhs_const.int_value
                                             : lhs_const.float_value * rhs_const.float_value;
                }
            } else {
                const_expr.is_valid = false;
            }
        } else {
            if (lhs->get_type()->is_float_type()) {
                if (rhs->get_type()->is_integer_type())
                    rhs = this->builder->create_sitofp(rhs, FLOAT_T);
                tmp_val = this->builder->create_fmul(lhs, rhs);
            } else if (rhs->get_type()->is_float_type()) {
                lhs = this->builder->create_sitofp(lhs, FLOAT_T);
                tmp_val = this->builder->create_fmul(lhs, rhs);
            } else {
                tmp_val = builder->create_imul(lhs, rhs);
            }
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinOp::DIVIDE) {
        if (scope.in_global()) {
            if (lhs_const.is_valid && rhs_const.is_valid) {
                const_expr.is_valid = true;
                if (lhs_const.is_int) {
                    if (rhs_const.is_int) {
                        const_expr.is_int = true;
                        const_expr.int_value = lhs_const.int_value / rhs_const.int_value;
                    } else {
                        const_expr.is_int = false;
                        const_expr.float_value = lhs_const.int_value / rhs_const.float_value;
                    }
                } else {
                    const_expr.is_int = false;
                    const_expr.float_value = rhs_const.is_int
                                             ? lhs_const.float_value / rhs_const.int_value
                                             : lhs_const.float_value / rhs_const.float_value;
                }
            } else {
                const_expr.is_valid = false;
            }
        } else {
            if (lhs->get_type()->is_float_type()) {
                if (rhs->get_type()->is_integer_type())
                    rhs = this->builder->create_sitofp(rhs, FLOAT_T);
                tmp_val = this->builder->create_fdiv(lhs, rhs);
            } else if (rhs->get_type()->is_float_type()) {
                lhs = this->builder->create_sitofp(lhs, FLOAT_T);
                tmp_val = this->builder->create_fdiv(lhs, rhs);
            } else {
                tmp_val = this->builder->create_isdiv(lhs, rhs);
            }
            const_expr.is_valid = false;
        }
    } else if (node.op == SyntaxTree::BinOp::MODULO) {
        if (scope.in_global()) {
            if (lhs_const.is_valid && rhs_const.is_valid) {
                const_expr.is_valid = true;
                const_expr.is_int = true;
                const_expr.int_value = lhs_const.int_value % rhs_const.int_value;
            } else {
                const_expr.is_valid = false;
            }
        } else {
            tmp_val = this->builder->create_isrem(lhs, rhs);
            const_expr.is_valid = false;
        }
    }
}

void IRBuilder::visit(SyntaxTree::UnaryExpr &node) {
    node.rhs->accept(*this);
    if (node.op == SyntaxTree::UnaryOp::MINUS) {
        if (scope.in_global()) {
            if (const_expr.is_valid) {
                if (const_expr.is_int)
                    const_expr.int_value = -const_expr.int_value;
                else
                    const_expr.float_value = -const_expr.float_value;
            }
        } else {
            if (tmp_val->get_type()->is_float_type())
                tmp_val = this->builder->create_fsub(CONST_FLOAT(0.0), tmp_val);
            else
                tmp_val = this->builder->create_isub(CONST_INT(0), tmp_val);
            const_expr.is_valid = false;
        }
    }
}

void IRBuilder::visit(SyntaxTree::FuncCallStmt &node) {
    Function *ret = dynamic_cast<Function *>(this->scope.find(node.name, true));
    std::vector<Value *> params{}; // 实参集合
    auto arg_begin = ret->arg_begin();
    for (const auto &expr : node.params) { // 对每个实参进行求值，并放到 params 中
        expr->accept(*this);
        if ((*arg_begin)->get_type() == FLOAT_T) {
            if (tmp_val->get_type() == INT32_T)
                tmp_val = this->builder->create_sitofp(tmp_val, FLOAT_T);
            if (tmp_val->get_type() == INT1_T) {
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
                tmp_val = this->builder->create_sitofp(tmp_val, FLOAT_T);
            }
        }
        if ((*arg_begin)->get_type() == INT32_T) {
            if (tmp_val->get_type() == FLOAT_T)
                tmp_val = this->builder->create_fptosi(tmp_val, INT32_T);
            if (tmp_val->get_type() == INT1_T) {
                tmp_val = this->builder->create_zext(tmp_val, INT32_T);
            }
        }
        if ((*arg_begin)->get_type()->is_pointer_type()) {
            tmp_val = tmp_addr;
            tmp_val->set_type((*arg_begin)->get_type());
        }
        params.push_back(tmp_val);
        arg_begin++;
    }
    tmp_val = this->builder->create_call(ret, std::move(params));
}

void IRBuilder::visit(SyntaxTree::IfStmt &node) {
    auto trueBB = BasicBlock::create(this->builder->get_module(), "IfTrue" + std::to_string(label++), this->builder->get_module()->get_functions().back());
    auto nextBB = BasicBlock::create(this->builder->get_module(), "IfNext" + std::to_string(label++), this->builder->get_module()->get_functions().back());
    if(node.else_statement==nullptr){
        node.cond_exp->accept(*this);
        if(tmp_val->get_type()==INT32_T){
            tmp_val = builder->create_icmp_ne(tmp_val,CONST_INT(0));
        }
        else if(tmp_val->get_type()==FLOAT_T){
            tmp_val = builder->create_fcmp_ne(tmp_val,CONST_FLOAT(0));
        }
        this->builder->create_cond_br(tmp_val, trueBB, nextBB);
        this->builder->set_insert_point(trueBB);
        node.if_statement->accept(*this);
        this->builder->create_br(nextBB);
        this->builder->set_insert_point(nextBB);
    }
    else{
        auto falseBB = BasicBlock::create(this->builder->get_module(), "IfFalse" + std::to_string(label++), this->builder->get_module()->get_functions().back());
        node.cond_exp->accept(*this);
        if(tmp_val->get_type()==INT32_T){
            tmp_val = builder->create_icmp_ne(tmp_val,CONST_INT(0));
        }
        else if(tmp_val->get_type()==FLOAT_T){
            tmp_val = builder->create_fcmp_ne(tmp_val,CONST_FLOAT(0));
        }
        this->builder->create_cond_br(tmp_val, trueBB, falseBB);
        this->builder->set_insert_point(trueBB);
        node.if_statement->accept(*this);
        if (builder->get_insert_block()->get_terminator() == nullptr)
            this->builder->create_br(nextBB);
        else
            nextBB->erase_from_parent();

        this->builder->set_insert_point(falseBB);
        node.else_statement->accept(*this);
        if (builder->get_insert_block()->get_terminator() == nullptr)
            this->builder->create_br(nextBB);

        this->builder->set_insert_point(nextBB);
    }
}

void IRBuilder::visit(SyntaxTree::WhileStmt &node) {
    auto condBB=BasicBlock::create(this->builder->get_module(),"WhileCond" + std::to_string(label++),this->builder->get_module()->get_functions().back());
    auto trueBB=BasicBlock::create(this->builder->get_module(),"WhileTrue" + std::to_string(label++),this->builder->get_module()->get_functions().back());
    auto falseBB=BasicBlock::create(this->builder->get_module(),"WhileFalse" + std::to_string(label++),this->builder->get_module()->get_functions().back());
    this->builder->create_br(condBB);
    this->builder->set_insert_point(condBB);

    node.cond_exp->accept(*this);
    if (tmp_val->get_type() == INT32_T){
        tmp_val = builder->create_icmp_ne(tmp_val,CONST_INT(0));
    }
    else if(tmp_val->get_type()==FLOAT_T){
        tmp_val = builder->create_fcmp_ne(tmp_val,CONST_FLOAT(0));
    }
    this->builder->create_cond_br(tmp_val, trueBB, falseBB);
    this->builder->set_insert_point(trueBB);
    tmp_condbb.push_back(condBB);
    tmp_falsebb.push_back(falseBB);

    node.statement->accept(*this);
    this->builder->create_br(condBB);
    this->builder->set_insert_point(falseBB);
    tmp_condbb.pop_back();
    tmp_falsebb.pop_back();
}

void IRBuilder::visit(SyntaxTree::BreakStmt &node) {
    this->builder->create_br(tmp_falsebb.back());
}

void IRBuilder::visit(SyntaxTree::ContinueStmt &node) {
    this->builder->create_br(tmp_condbb.back());
}

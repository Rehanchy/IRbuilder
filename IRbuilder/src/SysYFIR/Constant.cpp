#include "Constant.h"
#include "Module.h"
#include <iostream>
#include <string>
#include <sstream>

ConstantInt *ConstantInt::get(int val, Module *m)
{
    return new ConstantInt(Type::get_int32_type(m), val);
}
ConstantInt *ConstantInt::get(bool val, Module *m)
{
    return new ConstantInt(Type::get_int1_type(m),val?1:0);
}
std::string ConstantInt::print()
{
    std::string const_ir;
    Type *ty = this->get_type();
    if ( ty->is_integer_type() && static_cast<IntegerType *>(ty)->get_num_bits() == 1 )
    {
        //int1
        const_ir += (this->get_value() == 0) ? "false" : "true";
    }
    else
    {
        //int32
        const_ir += std::to_string(this->get_value());
    }
    return const_ir;
}

ConstantFloat *ConstantFloat::get(float val, Module *m)
{
    return new ConstantFloat(Type::get_float_type(m), val);
}
std::string ConstantFloat::print()
{
    std::stringstream fp_ir_ss;
    std::string fp_ir;
    double val = this->get_value();
    fp_ir_ss << "0x"<< std::hex << *(uint64_t *)&val << std::endl;
    fp_ir_ss >> fp_ir; 
    return fp_ir;
}

ConstantArray::ConstantArray(ArrayType *ty, const std::vector<Constant*> &val)
    : Constant(ty, "", val.size()) 
{
    for (int i = 0; i < val.size(); i++)
        set_operand(i, val[i]);
    this->const_array.assign(val.begin(),val.end());
}

Constant *ConstantArray::get_element_value(int index) {
    return this->const_array[index];
}

ConstantArray *ConstantArray::get(ArrayType *ty, const std::vector<Constant*> &val)
{
    return new ConstantArray(ty, val);
}

std::string ConstantArray::print()
{
    std::string const_ir;
    const_ir += "[";
    const_ir += this->get_type()->get_array_element_type()->print();
    const_ir += " ";
    const_ir += get_element_value(0)->print();
    for ( int i = 1 ; i < this->get_size_of_array() ; i++ ){
        const_ir += ", ";
        const_ir += this->get_type()->get_array_element_type()->print();
        const_ir += " ";
        const_ir += get_element_value(i)->print();
    }
    const_ir += "]";
    return const_ir;
}

/* Modified */
ConstantMultiArray::ConstantMultiArray(MultiDimensionArrayType *ty, std::vector<int> dimension_vec, const std::vector<std::vector<Constant*>> &val, int size)
    : Constant(ty, "", size) 
{
    int i = 0, j = 0, nums = 0;
    while(nums < size)
    {
        set_operand(nums, val[j][i]);
        nums++;
        i++;
        if(i == val[j].size())
        {
            i = 0;
            j++;
        }
    }
    this->const_multi_array.assign(val.begin(), val.end());
    this->dimension_vec.assign(dimension_vec.begin(), dimension_vec.end());
    this->size = size;
}

Constant *ConstantMultiArray::get_element_value(std::vector<int> gep_vec) {
    auto temp_vec = this->const_multi_array;
    auto dimension_vec = this->dimension_vec; // for calculation
    int i = gep_vec.size() - 2;
    int pos = 0;
    while(i >= 0) // not examined
    {
        int times = 1;
        int j = i;
        while(j < gep_vec.size() - 2)
        {
            times = times * dimension_vec[j];
            j++;
        }
        pos += gep_vec[i] * times;
        i--;
    }
    auto real_vec = temp_vec[pos];
    return real_vec[gep_vec.back()];
}

ConstantMultiArray *ConstantMultiArray::get(MultiDimensionArrayType *ty, std::vector<int> dimension_vec, const std::vector<std::vector<Constant*>> &val, int size)
{
    return new ConstantMultiArray(ty, dimension_vec, val, size);
}

std::string ConstantMultiArray::print()
{
    std::string const_ir;
    int level = this->dimension_vec.size();  // it is just like initial value // level = 1 is the base
    int level_static = this->dimension_vec.size();
    int PrintPos = 0; // tell you where we are
    int PrintCoord = (this->size/(this->dimension_vec.back())); 
    int level_flag = 0;
    int array[level+1];
    while(PrintPos < PrintCoord)
    {
        if(level > 1) // in the middle
        {
            if(array[level] != 1)
            {
                const_ir += "[";
                array[level] = 1; // means we have printed the upperest "["
            }    
            level--; // deeper
            int temp_level = level;
            while(temp_level >= 1)
            {
                const_ir += "[";
                const_ir += std::to_string(this->dimension_vec[level_static - temp_level]);
                const_ir += " x ";
                temp_level--;
            } // print pointer type
            const_ir += this->get_type()->get_array_element_type()->print();
            temp_level = level;
            while(temp_level >= 1)
            {
                const_ir += "]";
                temp_level--;
                if(temp_level == 0)
                    const_ir += " ";
            }
            level_flag = 0;
        }
        else // level == 1
        {
            const_ir += "[";
            int initial_size = this->dimension_vec[level_static-1]; // last dimension
            int position = 0;
            while(position < initial_size)
            {
                const_ir += this->get_type()->get_array_element_type()->print();
                const_ir += " ";
                int gep_pos = PrintPos;
                std::vector<int> Gep;
                int j = 1;
                std::vector <int> temp_value_vec;
                int temp_len = gep_pos;
                while(temp_len != 0)
                {
                    int temp = temp_len % this->dimension_vec[level_static - j - 1];
                    temp_value_vec.push_back(temp);
                    temp_len = temp_len/this->dimension_vec[level_static - j - 1];
                    j++;
                    if(level_static - j - 1 < 0)
                    break; // avoid error
                }
                int fulfill_need = level_static - temp_value_vec.size() - 1;
                while(fulfill_need != 0)
                {
                    Gep.push_back(0);
                    fulfill_need--;
                }
                while(temp_value_vec.size() != 0)
                {
                    auto t = temp_value_vec.back();
                    temp_value_vec.pop_back();
                    Gep.push_back(t);
                }
                Gep.push_back(position);
                const_ir += get_element_value(Gep)->print();
                position++;
                if(position == initial_size)
                    break;
                const_ir += ", ";
            }
            /** It's not that simple by adding, we need to check if it needs to raise **/
            int flag = 1;
            int j = this->dimension_vec.size()-2;
            int CalPos = PrintPos + 1;
            //int upper_flag = 1;
            if(CalPos%(this->dimension_vec[j])) // means we still in deepset part
                level_flag = 1;
            else
                level_flag = 0;
            while(flag)
            {
                if(j < 0)
                    break;
                if(CalPos%(this->dimension_vec[j]))
                {
                    flag = 0;
                    const_ir += "], ";
                    level++;
                }
                else
                {
                    flag = 1;
                    const_ir += "]";
                    array[level] = 0;
                    level++;
                    CalPos = CalPos/(this->dimension_vec[j]);
                    if(CalPos == 1)
                        array[level] = 0;
                    j--;
                }
            }
            PrintPos++;
        }
    }
    while(level <= level_static)
    {
        level++;
        const_ir += "]";
    }
    return const_ir;
}

ConstantZero *ConstantZero::get(Type *ty, Module *m) 
{
    return new ConstantZero(ty);
}

std::string ConstantZero::print()
{
    return "zeroinitializer";
}

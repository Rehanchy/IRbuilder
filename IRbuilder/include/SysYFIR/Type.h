#ifndef _SYSYF_TYPE_H_
#define _SYSYF_TYPE_H_

#include <vector>
#include <iostream>

class Module;
class IntegerType;
class FloatType;
class FunctionType;
class ArrayType;
class PointerType;
class MultiDimensionArrayType;

class Type
{
public:
    enum TypeID {
        VoidTyID,         // Void
        LabelTyID,        // Labels, e.g., BasicBlock
        IntegerTyID,      // Integers, include 32 bits and 1 bit
        FloatTyID,        // Floats, only 32 bits
        FunctionTyID,     // Functions
        ArrayTyID,        // Arrays
        PointerTyID,      // Pointer
        /* modified */
        MultiArrayTyID,
    };

    explicit Type(TypeID tid, Module *m);
    ~Type() = default;

    TypeID get_type_id() const { return tid_; }

    bool is_void_type() const { return get_type_id() == VoidTyID; }
    
    bool is_label_type() const { return get_type_id() == LabelTyID; }

    bool is_integer_type() const { return get_type_id() == IntegerTyID; }

    bool is_float_type() const { return get_type_id() == FloatTyID; }
        
    bool is_function_type() const { return get_type_id() == FunctionTyID; }

    bool is_array_type() const { return get_type_id() == ArrayTyID; }

    bool is_multi_array_type() const { return get_type_id() == MultiArrayTyID; }

    bool is_pointer_type() const { return get_type_id() == PointerTyID; }

    static bool is_eq_type(Type *ty1, Type *ty2);

    static Type *get_void_type(Module *m);

    static Type *get_label_type(Module *m);

    static IntegerType *get_int1_type(Module *m);

    static IntegerType *get_int32_type(Module *m);

    static FloatType *get_float_type(Module *m);

    static PointerType *get_int32_ptr_type(Module *m);

    static PointerType *get_float_ptr_type(Module *m);

    static PointerType *get_pointer_type(Type *contained);

    static ArrayType *get_array_type(Type *contained, unsigned num_elements);
    /* modified */
    static MultiDimensionArrayType *get_multi_array_type(Type *contained, std::vector<int> elements_array, unsigned dimension);

    Type *get_pointer_element_type();

    Type *get_array_element_type();

    int get_size();
    
    Module *get_module();

    std::string print();

private:
    TypeID tid_;
    Module *m_;
};

class IntegerType : public Type {
public:
    explicit IntegerType(unsigned num_bits ,Module *m);

    static IntegerType *get(unsigned num_bits, Module *m );

    unsigned get_num_bits();
private:
    unsigned num_bits_;
};

class FloatType : public Type {
public:
    FloatType(Module *m);
    static FloatType *get(Module *m );
};

class FunctionType : public Type {
public:
    FunctionType(Type *result, std::vector<Type *> params);

    static bool is_valid_return_type(Type *ty);
    static bool is_valid_argument_type(Type *ty);

    static FunctionType *get(Type *result,
                            std::vector<Type*> params);

    unsigned get_num_of_args() const;

    Type *get_param_type(unsigned i) const;
    std::vector<Type *>::iterator param_begin() { return args_.begin(); }
    std::vector<Type *>::iterator param_end() { return args_.end(); }
    Type *get_return_type() const;
private:
    Type *result_;
    std::vector<Type *> args_;
};

class ArrayType : public Type {
public:
    ArrayType(Type *contained, unsigned num_elements);

    static bool is_valid_element_type(Type *ty);

    static ArrayType *get(Type *contained, unsigned num_elements);

    Type *get_element_type() const { return contained_; }
    unsigned get_num_of_elements() const { return num_elements_; }

private:
    Type *contained_;   // The element type of the array.
    unsigned num_elements_;  // Number of elements in the array.
}; 
// create a new type for multi-dimension array

class MultiDimensionArrayType : public Type {
public:
    MultiDimensionArrayType(Type *contained, std::vector<int> elements_array, unsigned dimension);

    static bool is_valid_element_type(Type *ty);

    static MultiDimensionArrayType *get(Type *contained, std::vector<int> elements_array, unsigned dimension);

    Type *get_element_type() const { return contained_; }
    unsigned get_num_of_dimension() const { return dimension; }
    std::vector<int> get_dim_vec() const { return elements_array;}
    unsigned get_num_of_elements_by_dimension(int dimension) const { return elements_array[dimension]; } // seg fault be watched
private:
    Type *contained_;
    std::vector<int> elements_array; // could?
    unsigned dimension;
};

class PointerType : public Type {
public:
    PointerType(Type *contained);
    Type *get_element_type() const { return contained_; }

    static PointerType *get(Type *contained);

private:
    Type *contained_;   // The element type of the ptr.
};

#endif // _SYSYF_TYPE_H_
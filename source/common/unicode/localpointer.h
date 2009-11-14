/*
*******************************************************************************
*
*   Copyright (C) 2009, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  localpointer.h
*   encoding:   US-ASCII
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2009nov13
*   created by: Markus W. Scherer
*/

#ifndef __LOCALPOINTER_H__
#define __LOCALPOINTER_H__

/**
 * \file 
 * \brief C++ API: "Smart pointers" for use with and in ICU4C C++ code.
 */

#include "unicode/utypes.h"

#ifdef XP_CPLUSPLUS

U_NAMESPACE_BEGIN

/**
 * "Smart pointer" base class; do not use directly: use LocalPointer etc.
 *
 * Base class for smart pointer classes that do not throw exceptions.
 *
 * Do not use this base class directly, since it does not delete its pointer.
 * A subclass must implement methods that delete the pointer:
 * Destructor and adoptInstead().
 *
 * There is no operator T *() provided because the programmer must decide
 * whether to use getAlias() (without transfer of ownership) or orpan()
 * (with transfer of ownership and NULLing of the pointer).
 *
 * @see LocalPointer
 * @see LocalArray
 * @see U_DEFINE_LOCAL_OPEN_POINTER
 * @draft ICU 4.4
 */
template<typename T>
class LocalPointerBase {
public:
    /**
     * Constructor takes ownership.
     * @param p simple pointer to an object that is adopted
     * @draft ICU 4.4
     */
    explicit LocalPointerBase(T *p=NULL) : ptr(p) {}
    /**
     * Destructor deletes the object it owns.
     * Subclass must override: Base class does nothing.
     * @draft ICU 4.4
     */
    ~LocalPointerBase() { /* delete ptr; */ }
    /**
     * NULL check.
     * @return TRUE if ==NULL
     * @draft ICU 4.4
     */
    UBool isNull() const { return ptr==NULL; }
    /**
     * NULL check.
     * @return TRUE if !=NULL
     * @draft ICU 4.4
     */
    UBool isValid() const { return ptr!=NULL; }
    /**
     * Comparison with a simple pointer, so that existing code
     * with ==NULL need not be changed.
     * @param other simple pointer for comparison
     * @return true if this pointer value equals other
     * @draft ICU 4.4
     */
    bool operator==(const T *other) const { return ptr==other; }
    /**
     * Comparison with a simple pointer, so that existing code
     * with !=NULL need not be changed.
     * @param other simple pointer for comparison
     * @return true if this pointer value differs from other
     * @draft ICU 4.4
     */
    bool operator!=(const T *other) const { return ptr!=other; }
    /**
     * Access without ownership change.
     * @return the pointer value
     * @draft ICU 4.4
     */
    T *getAlias() const { return ptr; }
    /**
     * Access without ownership change.
     * @return the pointer value as a reference
     * @draft ICU 4.4
     */
    T &operator*() const { return *ptr; }
    /**
     * Access without ownership change.
     * @return the pointer value
     * @draft ICU 4.4
     */
    T *operator->() const { return ptr; }
    /**
     * Gives up ownership; the internal pointer becomes NULL.
     * @return the pointer value;
     *         caller becomes responsible for deleting the object
     * @draft ICU 4.4
     */
    T *orphan() {
        T *p=ptr;
        ptr=NULL;
        return p;
    }
    /**
     * Deletes the object it owns,
     * and adopts (takes ownership of) the one passed in.
     * Subclass must override: Base class does not delete the object.
     * @param p simple pointer to an object that is adopted
     * @draft ICU 4.4
     */
    void adoptInstead(T *p) {
        // delete ptr;
        ptr=p;
    }
protected:
    T *ptr;
private:
    // No comparison operators with other LocalPointerBase's.
    bool operator==(const LocalPointerBase &other);
    bool operator!=(const LocalPointerBase &other);
    // No ownership transfer: No copy constructor, no assignment operator.
    LocalPointerBase(const LocalPointerBase &other);
    void operator=(const LocalPointerBase &other);
    // No heap allocation. Use only on the stack.
    static void * U_EXPORT2 operator new(size_t size);
    static void * U_EXPORT2 operator new[](size_t size);
#if U_HAVE_PLACEMENT_NEW
    static void * U_EXPORT2 operator new(size_t, void *ptr);
#endif
};

/**
 * "Smart pointer" class, deletes objects via the standard C++ delete operator.
 * For most methods see the LocalPointerBase base class.
 *
 * @see LocalPointerBase
 * @draft ICU 4.4
 */
template<typename T>
class LocalPointer : public LocalPointerBase<T> {
public:
    /**
     * Constructor takes ownership.
     * @param p simple pointer to an object that is adopted
     * @draft ICU 4.4
     */
    explicit LocalPointer(T *p=NULL) : LocalPointerBase<T>(p) {}
    /**
     * Destructor deletes the object it owns.
     * @draft ICU 4.4
     */
    ~LocalPointer() {
        delete LocalPointerBase<T>::ptr;
    }
    /**
     * Deletes the object it owns,
     * and adopts (takes ownership of) the one passed in.
     * @param p simple pointer to an object that is adopted
     * @draft ICU 4.4
     */
    void adoptInstead(T *p) {
        delete LocalPointerBase<T>::ptr;
        LocalPointerBase<T>::ptr=p;
    }
};

/**
 * "Smart pointer" class, deletes objects via the C++ array delete[] operator.
 * For most methods see the LocalPointerBase base class.
 * Adds operator[] for array item access.
 *
 * @see LocalPointerBase
 * @draft ICU 4.4
 */
template<typename T>
class LocalArray : public LocalPointerBase<T> {
public:
    /**
     * Constructor takes ownership.
     * @param p simple pointer to an array of T objects that is adopted
     * @draft ICU 4.4
     */
    explicit LocalArray(T *p=NULL) : LocalPointerBase<T>(p) {}
    /**
     * Destructor deletes the array it owns.
     * @draft ICU 4.4
     */
    ~LocalArray() {
        delete[] LocalPointerBase<T>::ptr;
    }
    /**
     * Deletes the array it owns,
     * and adopts (takes ownership of) the one passed in.
     * @param p simple pointer to an array of T objects that is adopted
     * @draft ICU 4.4
     */
    void adoptInstead(T *p) {
        delete[] LocalPointerBase<T>::ptr;
        LocalPointerBase<T>::ptr=p;
    }
    /**
     * Array item access (writable).
     * No index bounds check.
     * @param i array index
     * @return reference to the array item
     * @draft ICU 4.4
     */
    T &operator[](ptrdiff_t i) const { return LocalPointerBase<T>::ptr[i]; }
};

/**
 * \def U_DEFINE_LOCAL_OPEN_POINTER
 * "Smart pointer" definition macro, deletes objects via the closeFunction.
 * Defines a subclass of LocalPointerBase which works just
 * like LocalPointer<Type> except that this subclass will use the closeFunction
 * rather than the C++ delete operator.
 *
 * Requirement: The closeFunction must tolerate a NULL pointer.
 * (We could add a NULL check here but it is normally redundant.)
 *
 * @see LocalPointerBase
 * @see LocalPointer
 * @draft ICU 4.4
 */
#define U_DEFINE_LOCAL_OPEN_POINTER(LocalPointerClassName, Type, closeFunction) \
    class LocalPointerClassName : public LocalPointerBase<Type> { \
    public: \
        explicit LocalPointerClassName(Type *p=NULL) : LocalPointerBase<Type>(p) {} \
        ~LocalPointerClassName() { closeFunction(ptr); } \
        void adoptInstead(Type *p) { \
            closeFunction(ptr); \
            ptr=p; \
        } \
    }

U_NAMESPACE_END

#endif  // XP_CPLUSPLUS
#endif  // __LOCALPOINTER_H__

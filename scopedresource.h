#ifndef SCOPEDRESOURCE_H
#define SCOPEDRESOURCE_H

#include <functional>

template<class T, class Err>
class ScopedResource
{
public:
    using Deleter = std::function<void(T *t, const Err &err)>;

    ScopedResource(std::function<void(T *&ptr, Err &err)> alloc, Deleter del) {
        alloc(p, err);
        this->del = del;
    }

    ~ScopedResource() {
        del(p, err);
    }

    const Err &error() {
        return err;
    }

    T *get() {
        return p;
    }

    T *operator->() {
        return p;
    }

private:
    Err err;
    T *p;
    Deleter del;
};

#endif // SCOPEDRESOURCE_H

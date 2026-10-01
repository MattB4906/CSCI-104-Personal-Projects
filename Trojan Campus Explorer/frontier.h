#ifndef FRONTIER_H
#define FRONTIER_H

template <typename T>
class Frontier {
    public:
        virtual ~Frontier();

        virtual void push(const T& value) = 0;
        virtual T pop() = 0;
        virtual bool empty() const = 0; 
};

#endif
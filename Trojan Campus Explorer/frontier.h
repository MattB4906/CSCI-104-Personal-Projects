#ifndef FRONTIER_H
#define FRONTIER_H

#include <deque>
#include <queue>
#include <vector>
#include <stdexcept>

template <typename T>
class Frontier {
    public:
        virtual ~Frontier() {};

        virtual void push(const T& value) = 0;
        virtual T pop() = 0;
        virtual bool empty() const = 0; 
};

template <typename T>
class FifoFrontier : public Frontier<T> {
    public:
        void push(const T& value) override {
            data_.push_back(value);
        }

        T pop() override {
            if(data_.empty()) {
                std::out_of_range("No data");
            }

            T temp = data_.front();
            data_.pop_front();

            return temp;
        }

        bool empty() const override {
            return data_.empty();
        }

    private:
    std::deque<T> data_;
};

template <typename T, typename Compare>
class PriorityFrontier : public Frontier<T> {
    public:
        void push(const T& value) override {
            data_.push(value);
        }

        T pop() override {
            if(data_.empty()) {
                std::out_of_range("No data");
            }

            T temp = data_.top();
            data_.pop();

            return temp;
        }

        bool empty() const override {
            return data_.empty();
        }

    private:
        std::priority_queue<T, std::vector<T>, Compare> data_;
};

#endif
#ifndef QUEUE_H
#define QUEUE_H
#include <queue>
#include <mutex>
#include <condition_variable>

namespace cocochick{
    template<typename T>
    class queue{
    public:
        queue() = default;
        queue(const std::queue<T>& lhs) : box(lhs){}
        queue(std::queue<T>&& lhs) : box(std::move(lhs)){}
        void push(const T& elem){
            std::lock_guard<std::mutex> lk(mut);
            box.push(elem);
            cv.notify_one();
        }
        void push(T&& elem){
            std::lock_guard<std::mutex> lk(mut);
            box.push(std::move(elem));
            cv.notify_one();
        }
        std::shared_ptr<T> wait_and_pop(){
            std::unique_lock<std::mutex> lk(mut);
            cv.wait(lk, [this](){return !box.empty();});
            std::shared_ptr<T> res(std::make_shared(std::move(box.front())));
            box.pop();
            return res;
        }
        void wait_and_pop(T& value){
            std::unique_lock<std::mutex> lk(mut);
            cv.wait(lk, [this](){return !box.empty();});
            value = std::move(box.front());
            box.pop();
        }
        bool try_pop(T& value){
            std::lock_guard<std::mutex> lk(mut);
            if(box.empty()){
                return false;
            }
            value = std::move(box.front());
            box.pop();
            return true;
        }
        std::shared_ptr<T> try_pop(){
            std::lock_guard<std::mutex> lk(mut);
            if(box.empty()){
                return std::shared_ptr<T>{};
            }
            std::shared_ptr<T> res(std::make_shared(std::move(box.front())));
            box.pop();
            return res;
        }
        bool empty() const{
            std::lock_guard<std::mutex> lk(mut);
            return box.empty();
        }
        
    private:
        std::queue<T> box;
        std::mutex mut;
        std::condition_variable cv;
    };
};

#endif
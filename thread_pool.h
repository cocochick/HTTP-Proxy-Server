#pragma once

#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <future>
#include "queue.h"

//供packaged_task使用的function
class function_wrapper{
public:
    function_wrapper() = default;
    template<typename F>
    function_wrapper(F&& f) : impl(new impl_type<F>(std::move(f))){}
    void operator()(){
        impl->call();
    }
    function_wrapper(function_wrapper&& lhs) : impl(std::move(lhs.impl)){}
    function_wrapper& operator=(function_wrapper&& lhs){
        impl = std::move(lhs.impl);
        return *this;
    }
private:
    struct impl_base{
        virtual void call() = 0;
        virtual ~impl_base(){};
    };
    template<typename F>
    struct impl_type : impl_base{
        F f;
        impl_type(F&& f_) : f(std::move(f_)){}
        void call(){
            f();
        }
    };
    std::unique_ptr<impl_base> impl;
};

class join_threads{
public:
    explicit join_threads(std::vector<std::thread>& threads_) : threads(threads){}
    ~join_threads(){
        for(size_t i = 0; i < threads.size(); ++i){
            if(threads[i].joinable()){
                threads[i].join();
            }
        }
    }

private:
    std::vector<std::thread>& threads;
};
class thread_pool{
public:
    thread_pool() : done(false), joiner(threads){
        const size_t thread_count = std::max(32u, std::thread::hardware_concurrency());
        try{
            for(size_t i = 0; i < thread_count; ++i){
                threads.push_back(std::thread(&thread_pool::work_thread, this));
            }
        }
        catch(...){
            done = true;
            throw;
        }
    }
    ~thread_pool(){
        done = true;
    }
    template<typename FunctionType>
    std::future<typename std::result_of<FunctionType()>::type> submit(FunctionType f){ 
        typedef typename std::result_of<FunctionType()>::type result_type;
        std::packaged_task<result_type()> task(std::move(f));
        std::future<result_type> res(task.get_future());
        work_queue.push(std::move(task));
        return res;
    }
private:
    std::atomic_bool done;
    cocochick::queue<function_wrapper> work_queue;
    std::vector<std::thread> threads;
    join_threads joiner;
    void work_thread(){
        while(!done){
            function_wrapper task;
            if(work_queue.try_pop(task)){
                task();
            }
            else{
                std::this_thread::yield();
            }
        }
    }
};
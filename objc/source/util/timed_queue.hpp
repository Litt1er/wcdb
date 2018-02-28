/*
 * Tencent is pleased to support the open source community by making
 * WCDB available.
 *
 * Copyright (C) 2017 THL A29 Limited, a Tencent company.
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *       https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef mymap_hpp
#define mymap_hpp

#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <stdio.h>
#include <string>
#include <thread>
#include <unordered_map>

namespace WCDB {

template <typename Key, typename Info>
class TimedQueue {
public:
    TimedQueue(int delay)
        : m_delay(std::chrono::seconds(delay)), m_break(false){};

    typedef std::function<void(const Key &, const Info &)> OnExpired;

    void reQueue(const Key &key, const Info &info)
    {
        std::lock_guard<std::mutex> lockGuard(m_mutex);
        bool signal = m_list.empty();

        unsafeRemove(key);

        //delay
        std::shared_ptr<Element> element(
            new Element(key, std::chrono::steady_clock::now() + m_delay, info));
        m_list.push_front(element);
        auto last = m_list.begin();
        m_map.insert({key, last});
        if (signal) {
            m_cond.notify_one();
        }
    }

    void remove(const Key &key)
    {
        std::lock_guard<std::mutex> lockGuard(m_mutex);
        unsafeRemove(key);
    }

    void notify()
    {
        m_break = true;
        m_cond.notify_one();
    }

    void waitUntilExpired(const OnExpired &onExpired)
    {
        {
            std::unique_lock<std::mutex> lockGuard(m_mutex);
            while (m_list.empty()) {
                if (!m_break) {
                    m_cond.wait(lockGuard);
                } else {
                    return;
                }
            }
        }
        bool get = false;
        while (!get) {
            std::shared_ptr<Element> element;
            Time now = std::chrono::steady_clock::now();
            {
                std::unique_lock<std::mutex> lockGuard(m_mutex);
                element = m_list.back();
                if (now > element->time) {
                    m_list.pop_back();
                    m_map.erase(element->key);
                    get = true;
                }
            }
            if (get) {
                onExpired(element->key, element->info);
            } else {
                std::this_thread::sleep_for(element->time - now);
            }
        }
    }

protected:
    using Time = std::chrono::steady_clock::time_point;
    struct Element {
        Element(const Key &key_, const Time &time_, const Info &info_)
            : key(key_), time(time_), info(info_)
        {
        }
        Key key;
        Time time;
        Info info;
    };
    typedef struct Element Element;
    using List = std::list<std::shared_ptr<Element>>;
    using Map = std::unordered_map<Key, typename List::iterator>;
    Map m_map;
    List m_list;
    std::condition_variable m_cond;
    std::mutex m_mutex;
    std::chrono::seconds m_delay;
    std::atomic<bool> m_break;

    void unsafeRemove(const Key &key)
    {
        auto iter = m_map.find(key);
        if (iter != m_map.end()) {
            m_list.erase(iter->second);
            m_map.erase(iter);
        }
    }
};

} //namespace WCDB

#endif /* mymap_hpp */

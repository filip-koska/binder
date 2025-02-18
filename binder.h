#ifndef BINDER_H
#define BINDER_H

#include <algorithm>
#include <utility>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <optional>
#include <cstddef>

namespace cxx {

template <typename K, typename V>
class binder {

    class binder_data;

    // data_ptr == nullptr indicates empty binder
    std::shared_ptr<binder_data> data_ptr;

    // indicator of whether there exists a non-const reference to the object's data
    bool read_called;

    // returns the appropriate temporary pointer based on passed condition
    decltype(auto) get_new_unique_shared(std::shared_ptr<binder_data> const& old_ptr, bool cond) {
        std::shared_ptr<binder_data> ret_val;
        if (old_ptr == nullptr)
            ret_val = std::make_shared<binder_data>();
        else if (cond)
            ret_val = old_ptr;
        else
            ret_val = std::make_shared<binder_data>(*old_ptr);
        return ret_val;
    }

public:

    binder() noexcept : data_ptr{}, read_called{} {}

    ~binder() noexcept {
        data_ptr = nullptr;
    }

    // performs deep copy if the copied-from object previously called non-const read()
    // to ensure proper COW
    binder(binder const& rhs) : data_ptr{}, read_called{} {
        data_ptr = rhs.read_called
            ? std::make_shared<binder_data>(*rhs.data_ptr)
            : rhs.data_ptr;
    }

    binder(binder&& rhs) noexcept : data_ptr{std::move(rhs.data_ptr)}, read_called{std::move(rhs.read_called)} {
        rhs.data_ptr = nullptr;
    }

    binder& operator=(binder const& rhs) {
        data_ptr = (rhs.data_ptr == nullptr || !rhs.read_called)
            ? rhs.data_ptr
            : std::make_shared<binder_data>(*rhs.data_ptr);
            
        read_called = false;
        return *this;
    }

    binder& operator=(binder&& rhs) noexcept {
        data_ptr = std::move(rhs.data_ptr);
        read_called = std::move(rhs.read_called);
        return *this;
    }

    void insert_front(K const& k, V const& v) {
        auto new_data_ptr = get_new_unique_shared(data_ptr, data_ptr.use_count() == 1);
        new_data_ptr->insert_front(k, v);
        read_called = false;
        data_ptr = new_data_ptr;
    }

    void insert_after(K const& prev_k, K const& k, V const& v) {
        if (data_ptr == nullptr)
            throw std::invalid_argument("binder is empty");
        auto new_data_ptr = get_new_unique_shared(data_ptr, data_ptr.use_count() == 1);
        new_data_ptr->insert_after(prev_k, k, v);
        read_called = false;
        data_ptr = new_data_ptr;
    }

    void remove() {
        if (data_ptr == nullptr)
            throw std::invalid_argument("binder is empty");

        auto new_data_ptr = get_new_unique_shared(data_ptr, data_ptr.use_count() == 1);
        new_data_ptr->remove();
        if (!new_data_ptr->size())
            new_data_ptr = nullptr;
        read_called = false;
        data_ptr = new_data_ptr;
    }

    void remove(K const& k) {
        if (data_ptr == nullptr)
            throw std::invalid_argument("binder is empty");

        auto new_data_ptr = get_new_unique_shared(data_ptr, data_ptr.use_count() == 1);
        new_data_ptr->remove(k);
        if (!new_data_ptr->size())
            new_data_ptr = nullptr;
        read_called = false;
        data_ptr = new_data_ptr;
    }

    V& read(K const& k) {
        if (data_ptr == nullptr)
            throw std::invalid_argument("binder is empty");

        auto new_data_ptr = get_new_unique_shared(data_ptr, data_ptr.use_count() == 1);
        V& res = new_data_ptr->read(k);
        data_ptr = new_data_ptr;
        read_called = true;
        return res;
    }

    V const& read(K const& k) const {
        if (data_ptr == nullptr)
            throw std::invalid_argument("binder is empty");

        return data_ptr->read_const(k);
    }

    std::size_t size() const noexcept {
        return (data_ptr == nullptr) ? 0 : data_ptr->size();
    }

    void clear() noexcept {
        data_ptr = nullptr;
        read_called = false;
    }

    class const_iterator;

    const_iterator cbegin() const noexcept {
        return (data_ptr == nullptr) ? const_iterator() : const_iterator(data_ptr->cbegin(), data_ptr.get());
    }

    const_iterator cend() const noexcept {
        return (data_ptr == nullptr) ? const_iterator() : const_iterator(data_ptr->cend(), data_ptr.get());
    }
}; // class binder

template <typename K, typename V>
class binder<K, V>::binder_data {

    std::list<std::pair<K, V>> content;
    using list_iterator_t = typename decltype(content)::iterator;
    std::map<K, list_iterator_t> address;

public:

    binder_data() : content{}, address{} {}

    binder_data(binder_data const& rhs) : content{rhs.content}, address{} {
        for (auto it = content.begin(); it != content.end(); ++it)
            address.insert(std::make_pair(it->first, it));
    }

    binder_data(binder_data&& rhs) noexcept : content{std::move(rhs.content)}, address{std::move(rhs.address)} {}

    void insert_front(K const& k, V const& v) {
        if (address.contains(k))
            throw std::invalid_argument("binder already contains entry with given key");
        content.push_front(std::make_pair(k, v));
        try {
            address.emplace(std::make_pair(k, content.begin()));
        }
        catch (...) {
            content.pop_front();
            throw;
        }
    }

    void insert_after(K const& prev_k, K const& k, V const& v) {
        if (address.contains(k))
            throw std::invalid_argument("binder already contains entry with given key");
        
        if (!address.contains(prev_k))
            throw std::invalid_argument("binder doesn't contain previous entry");
        auto prev_iter = address.find(prev_k)->second;
        ++prev_iter;
        auto new_iter = content.insert(prev_iter, std::make_pair(k, v));
        try {
            address.insert(std::make_pair(k, new_iter));
        }
        catch (...) {
            content.erase(new_iter);
            throw;
        }
    }

    void remove() {
        if (address.empty())
            throw std::invalid_argument("binder is empty");
        address.erase(content.begin()->first);
        content.erase(content.begin());
    }

    void remove(K const& k) {
        if (!address.contains(k))
            throw std::invalid_argument("note doesn't exist in binder");
        auto iter = address.find(k)->second;
        content.erase(iter);
        address.erase(k);
    }

    V& read(K const& k) {
        auto iter = address.find(k);
        if (iter == address.end())
            throw std::invalid_argument("note doesn't exist in binder");
        return iter->second->second;
    }

    V const& read_const(K const& k) const {
        auto iter = address.find(k);
        if (iter == address.end())
            throw std::invalid_argument("note doesn't exist in binder");
        return iter->second->second;
    }

    std::size_t size() const noexcept {
        return content.size();
    }

    auto cbegin() const noexcept {
        return content.cbegin();
    }

    auto cend() const noexcept {
        return content.cend();
    }

}; // class binder<K, V>::binder_data

template <typename K, typename V>
class binder<K, V>::const_iterator {
    using list_iterator_t = typename std::list<std::pair<K, V>>::const_iterator;

    friend class binder<K, V>;

    std::optional<list_iterator_t> list_iterator;

    // pointer to 
    binder<K, V>::binder_data const* obj_ptr;

    // is only used with it equal to the cbegin() of some list and therefore never throws
    explicit const_iterator(list_iterator_t it, auto* obj_id) noexcept : list_iterator{it}, obj_ptr{obj_id} {}


public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = V;
    using difference_type = std::ptrdiff_t;

    explicit const_iterator() noexcept : list_iterator{}, obj_ptr{} {}

    const_iterator(const_iterator const& rhs) : list_iterator{rhs.list_iterator}, obj_ptr{rhs.obj_ptr}  {}
    
    V const& operator*() const noexcept {
        return list_iterator.value()->second;
    }

    V const* operator->() const noexcept {
        return &(list_iterator.value()->second);
    }

    const_iterator& operator=(const_iterator const& rhs) {
        list_iterator = rhs.list_iterator;
        obj_ptr = rhs.obj_ptr;
        return *this;
    }

    const_iterator& operator=(const_iterator&& rhs) {
        list_iterator = std::move(rhs.list_iterator);
        obj_ptr = std::move(rhs.obj_ptr);
        return *this;
    }

    bool operator==(const_iterator const& rhs) const noexcept {
        return obj_ptr == rhs.obj_ptr && list_iterator == rhs.list_iterator;
    }

    bool operator!=(const_iterator const& rhs) const noexcept {
        return !(*this == rhs);
    }

    const_iterator& operator++() noexcept {
        ++list_iterator.value();
        return *this;
    }

    const_iterator operator++(int) noexcept {
        auto tmp = *this;
        ++list_iterator.value();
        return tmp;
    }
}; // class binder<K, V>::const_iterator

} // namespace cxx




#endif
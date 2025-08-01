////////////////////////////////////////////////////////////////////
// Atlantis Cache System - CacheModel (LRU Cache) - Kernel Mode Port
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This file was adapted from the original Atlantis project
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_CACHEMODEL_H
#define ATLANTIS_CACHEMODEL_H

#include "atlantis_types.h"

namespace Atlantis {

// Simple list node for LRU implementation
template<typename T>
struct ListNode {
    T data;
    ListNode* next;
    ListNode* prev;
    
    ListNode(const T& value) : data(value), next(nullptr), prev(nullptr) {}
};

// Simple list implementation for kernel mode
template<typename T>
class List {
private:
    ListNode<T>* head_;
    ListNode<T>* tail_;
    size_t size_;

public:
    class iterator {
    private:
        ListNode<T>* node_;
    public:
        iterator(ListNode<T>* node) : node_(node) {}
        T& operator*() { return node_->data; }
        T* operator->() { return &node_->data; }
        iterator& operator++() { node_ = node_->next; return *this; }
        bool operator==(const iterator& other) const { return node_ == other.node_; }
        bool operator!=(const iterator& other) const { return node_ != other.node_; }
        ListNode<T>* get_node() const { return node_; }
    };

    List() : head_(nullptr), tail_(nullptr), size_(0) {}
    
    ~List() {
        clear();
    }
    
    void push_front(const T& value) {
        ListNode<T>* new_node = new ListNode<T>(value);
        if (!head_) {
            head_ = tail_ = new_node;
        } else {
            new_node->next = head_;
            head_->prev = new_node;
            head_ = new_node;
        }
        ++size_;
    }
    
    void pop_back() {
        if (tail_) {
            ListNode<T>* to_delete = tail_;
            tail_ = tail_->prev;
            if (tail_) {
                tail_->next = nullptr;
            } else {
                head_ = nullptr;
            }
            delete to_delete;
            --size_;
        }
    }
    
    void erase(iterator it) {
        ListNode<T>* node = it.get_node();
        if (!node) return;
        
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        
        delete node;
        --size_;
    }
    
    iterator begin() { return iterator(head_); }
    iterator end() { return iterator(nullptr); }
    T& back() { return tail_->data; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    void splice(iterator pos, List& other, iterator it) {
        // Move node from other list to this list at pos
        ListNode<T>* node = it.get_node();
        if (!node) return;
        
        // Remove from other list
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            other.head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            other.tail_ = node->prev;
        }
        --other.size_;
        
        // Insert at beginning of this list
        node->next = head_;
        node->prev = nullptr;
        if (head_) {
            head_->prev = node;
        } else {
            tail_ = node;
        }
        head_ = node;
        ++size_;
    }
    
private:
    void clear() {
        while (head_) {
            ListNode<T>* next = head_->next;
            delete head_;
            head_ = next;
        }
        tail_ = nullptr;
        size_ = 0;
    }
};

// Simple hash map implementation for kernel mode
template<typename Key, typename Value>
class HashMap {
private:
    static const size_t BUCKET_COUNT = 1024;
    
    struct HashNode {
        Key key;
        Value value;
        HashNode* next;
        
        HashNode(const Key& k, const Value& v) : key(k), value(v), next(nullptr) {}
    };
    
    HashNode* buckets_[BUCKET_COUNT];
    size_t size_;
    
    size_t hash(const Key& key) const {
        // Simple hash function
        return static_cast<size_t>(key) % BUCKET_COUNT;
    }

public:
    class iterator {
    private:
        HashMap* map_;
        size_t bucket_;
        HashNode* node_;
        
        void find_next() {
            while (bucket_ < BUCKET_COUNT && !node_) {
                if (bucket_ < BUCKET_COUNT) {
                    node_ = map_->buckets_[bucket_];
                }
                if (!node_) {
                    ++bucket_;
                }
            }
        }
        
    public:
        iterator(HashMap* map, size_t bucket, HashNode* node) 
            : map_(map), bucket_(bucket), node_(node) {
            if (!node_) {
                find_next();
            }
        }
        
        Value& operator*() { return node_->value; }
        Value* operator->() { return &node_->value; }
        
        iterator& operator++() {
            if (node_) {
                node_ = node_->next;
                if (!node_) {
                    ++bucket_;
                    find_next();
                }
            }
            return *this;
        }
        
        bool operator==(const iterator& other) const {
            return bucket_ == other.bucket_ && node_ == other.node_;
        }
        
        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
        
        HashNode* get_node() const { return node_; }
    };

    HashMap() : size_(0) {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            buckets_[i] = nullptr;
        }
    }
    
    ~HashMap() {
        clear();
    }
    
    iterator find(const Key& key) {
        size_t bucket = hash(key);
        HashNode* node = buckets_[bucket];
        while (node) {
            if (node->key == key) {
                return iterator(this, bucket, node);
            }
            node = node->next;
        }
        return end();
    }
    
    iterator end() {
        return iterator(this, BUCKET_COUNT, nullptr);
    }
    
    void insert(const Key& key, const Value& value) {
        size_t bucket = hash(key);
        HashNode* new_node = new HashNode(key, value);
        new_node->next = buckets_[bucket];
        buckets_[bucket] = new_node;
        ++size_;
    }
    
    void erase(const Key& key) {
        size_t bucket = hash(key);
        HashNode** node = &buckets_[bucket];
        while (*node) {
            if ((*node)->key == key) {
                HashNode* to_delete = *node;
                *node = (*node)->next;
                delete to_delete;
                --size_;
                return;
            }
            node = &(*node)->next;
        }
    }
    
    Value& operator[](const Key& key) {
        iterator it = find(key);
        if (it != end()) {
            return *it;
        }
        
        // Insert new element
        size_t bucket = hash(key);
        HashNode* new_node = new HashNode(key, Value());
        new_node->next = buckets_[bucket];
        buckets_[bucket] = new_node;
        ++size_;
        return new_node->value;
    }
    
    size_t count(const Key& key) const {
        size_t bucket = hash(key);
        HashNode* node = buckets_[bucket];
        while (node) {
            if (node->key == key) {
                return 1;
            }
            node = node->next;
        }
        return 0;
    }
    
    size_t size() const { return size_; }

private:
    void clear() {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            HashNode* node = buckets_[i];
            while (node) {
                HashNode* next = node->next;
                delete node;
                node = next;
            }
            buckets_[i] = nullptr;
        }
        size_ = 0;
    }
};

//**************************************************************************************
//*		Interface ICacheModel
//*************************************************************************************
template<typename CACHEKEY, typename CACHEVALUE>
class ICacheModel {
public:
    using Pointer = shared_ptr<ICacheModel<CACHEKEY, CACHEVALUE>>;
    virtual ~ICacheModel() = default;

    virtual pair<bool, CACHEVALUE> getValue(CACHEKEY key) = 0;
    virtual pair<CACHEKEY, CACHEVALUE> setValue(CACHEKEY key, CACHEVALUE value) = 0;
};

//**************************************************************************************
//*		LRU Cache Implementation
//*************************************************************************************
template<typename CACHEKEY, typename CACHEVALUE>
class LRUCache : public ICacheModel<CACHEKEY, CACHEVALUE> {
public:
    using Pointer = shared_ptr<LRUCache<CACHEKEY, CACHEVALUE>>;
    using PairCache = pair<CACHEKEY, CACHEVALUE>;
    using ListCache = List<PairCache>;
    using MapCache = HashMap<CACHEKEY, typename ListCache::iterator>;

    template<typename ...Args>
    static Pointer create(Args &&...arg) {
        struct EnableMakeShared : public LRUCache {
            EnableMakeShared(Args &&...arg) : LRUCache(std::forward<Args>(arg)...) {}
        };
        Pointer result(new EnableMakeShared(std::forward<Args>(arg)...));
        return result;
    }

    virtual ~LRUCache() = default;

    LRUCache(const LRUCache &) = delete;
    LRUCache(LRUCache &&) = delete;
    const LRUCache& operator=(const LRUCache&) = delete;

    pair<CACHEKEY, CACHEVALUE> setValue(CACHEKEY key, CACHEVALUE val) override {
        auto it = itemMap_.find(key);

        if (it != itemMap_.end()) {
            itemList_.erase(*it);
            itemMap_.erase(key);
        }

        itemList_.push_front(make_pair(key, val));
        itemMap_.insert(key, itemList_.begin());

        return clean();
    }

    pair<bool, CACHEVALUE> getValue(CACHEKEY key) override {
        if (itemMap_.count(key) == 0) {
            CACHEVALUE empty;
            return make_pair(false, empty);
        }

        auto it = itemMap_.find(key);
        auto list_it = *it;
        
        // Move to front (most recently used)
        itemList_.splice(itemList_.begin(), itemList_, list_it);
        itemMap_[key] = itemList_.begin();

        return make_pair(true, list_it->second);
    }

protected:
    explicit LRUCache(size_t cacheSize) : cacheSize_(cacheSize) {}

private:
    PairCache clean() {
        PairCache pairClean;

        while (itemMap_.size() > cacheSize_) {
            if (!itemList_.empty()) {
                pairClean = itemList_.back();
                itemMap_.erase(pairClean.first);
                itemList_.pop_back();
            }
        }

        return pairClean;
    }

private:
    ListCache itemList_;
    MapCache itemMap_;
    size_t cacheSize_;
};

//**************************************************************************************
//*		CacheRecord specialization
//*************************************************************************************
class CacheRecord : public LRUCache<int, string> {
public:
    virtual ~CacheRecord() = default;
    CacheRecord(const CacheRecord &) = delete;
    CacheRecord(CacheRecord &&) = delete;
    const CacheRecord& operator=(const CacheRecord&) = delete;

private:
    explicit CacheRecord(size_t cacheSize) : LRUCache(cacheSize) {}
};

} // namespace Atlantis

#endif // ATLANTIS_CACHEMODEL_H
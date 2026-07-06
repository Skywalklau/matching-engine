#pragma once

#include "order.hpp"

#include <cstdint>

// ---------------------------------------------------------------------------
// OrderNode and Limit — shared internal types used by OrderBookV2 and OrderBook.
//
// Defined here to avoid duplicate definitions when both headers are
// included in the same compilation unit (e.g. benchmarks).
// ---------------------------------------------------------------------------

// A node in the doubly linked list at a single price level.
// The list is ordered FIFO: head = oldest (filled first), tail = newest.
struct OrderNode {
    Order      order;
    OrderNode* prev{nullptr};
    OrderNode* next{nullptr};
};

// A single price level. Owns a doubly linked list of OrderNodes.
struct Limit {
    uint32_t   price{0};
    OrderNode* head{nullptr}; // oldest order — filled first (time priority)
    OrderNode* tail{nullptr}; // newest order
    uint32_t   count{0};

    bool empty() const noexcept { return head == nullptr; }

    // Append node to the back (lowest time priority).
    void pushBack(OrderNode* node) noexcept {
        node->prev = tail;
        node->next = nullptr;
        if (tail) tail->next = node;
        else      head       = node; // list was empty — node is also head
        tail = node;
        ++count;
    }

    // Unlink node from any position in O(1).
    void remove(OrderNode* node) noexcept {
        if (node->prev) node->prev->next = node->next;
        else            head             = node->next; // node was head
        if (node->next) node->next->prev = node->prev;
        else            tail             = node->prev; // node was tail
        node->prev = nullptr;
        node->next = nullptr;
        --count;
    }
};

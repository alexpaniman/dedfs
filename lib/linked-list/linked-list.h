#pragma once

#include "trace.h"

#include <stdlib.h>
#include <stdbool.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

typedef int element_index_t;

template <typename E>
struct element {
    element_index_t next_index;
    element_index_t prev_index;
    bool is_free;

    E element;
};

template <typename E>
struct linked_list {
    element<E>* elements;
    size_t capacity, used;

    element_index_t free;
    bool is_linearized;
};


template <typename E>
inline element<E>* linked_list_next(linked_list<E>* list, element<E>* current) {
    return &list->elements[current->next_index];
}

template <typename E>
inline element<E>* linked_list_prev(linked_list<E>* list, element<E>* current) {
    return &list->elements[current->prev_index];
}

const element_index_t linked_list_end_index = 0;

template <typename E>
inline element<E>* linked_list_end(linked_list<E>* list) {
    return &list->elements[linked_list_end_index];
}


template <typename E>
inline element_index_t linked_list_head_index(linked_list<E>* list) {
    return linked_list_end(list)->next_index;
}

template <typename E>
inline element<E>* linked_list_head(linked_list<E>* list) {
    return &list->elements[linked_list_head_index(list)];
}


template <typename E>
inline element_index_t linked_list_tail_index(linked_list<E>* list) {
    return linked_list_end(list)->prev_index;
}

template <typename E>
inline element<E>* linked_list_tail(linked_list<E>* list) {
    return &list->elements[linked_list_tail_index(list)];
}


template <typename E>
inline element<E>* linked_list_get_pointer(linked_list<E>* list,
                                           element_index_t actual_index) {
    return &list->elements[actual_index];
}

template <typename E>
inline element_index_t linked_list_get_index(linked_list<E>* list,
                                             element<E>* element_ptr) {
    return element_ptr - list->elements;
}


template <typename E>
stack_trace* linked_list_create(linked_list<E>* list, const size_t capacity = 10) {
    element<E>* new_space = (element<E>*)
        calloc(capacity + 2 /* For two terminal nodes */, sizeof(*new_space));

    if (new_space == NULL)
        return FAILURE(RUNTIME_ERROR, strerror(errno));

    list->elements = new_space;
    list->capacity = capacity;

    list->is_linearized = true;

    // Memory is assumed to be zeroed after calloc
    linked_list_head(list)->is_free = false;

    // Loop first free element on itself
    list->free = 1;
    *linked_list_get_pointer(list, list->free) = 
        { .next_index = list->free,
          .prev_index = list->free,
          .is_free = true, .element = (E) {} };

    // Expand doubly linked list of free elements
    for (element_index_t i = (element_index_t) capacity + 1; i > list->free; -- i)
        __linked_list_insert_after_in_place(list, (E) {}, list->free, i);

    return SUCCESS();
}


template <typename E>
static inline
stack_trace* check_index(linked_list<E>* list, element_index_t index) {
    if (index > (element_index_t) list->capacity + 1)
        return FAILURE(RUNTIME_ERROR, "Index %d overflows list capacity %d!",
                       index, list->capacity);

    if (index < 0)
        return FAILURE(RUNTIME_ERROR, "Index %d underflows list capacity %d!",
                       index, list->capacity);

    return SUCCESS();
}


template <typename E>
stack_trace* linked_list_resize(linked_list<E>* list, const size_t new_capacity) {
    element<E>* new_space = (element<E>*)
        realloc(list->elements, sizeof(*new_space) *
                (new_capacity + 2) /* For terminal nodes */);

    if (new_space == NULL)
        return FAILURE(RUNTIME_ERROR, strerror(errno));

    list->elements = new_space;

    for (element_index_t i = list->capacity + 2; i <= (element_index_t) new_capacity + 1; ++ i)
        add_free_element(list, i);

    list->capacity = new_capacity;

    return SUCCESS();
}


template <typename E>
static inline
bool free_elements_left(linked_list<E>* list) {
    return list->free != list->elements[list->free].next_index;
}

template <typename E>
stack_trace* get_free_element_on_place(linked_list<E>* list,
                                       element_index_t place_index) {

    if (!is_free_element(list, place_index))
        return FAILURE(RUNTIME_ERROR, "Element %d isn't free!", place_index);

    const element_index_t next =
        list->elements[place_index].next_index;

    if (next == place_index)
        return FAILURE(RUNTIME_ERROR, "There's no free elements left!");

    TRY linked_list_unlink(list, place_index)
        FAIL("Failed to unlink element on place %d!", place_index);

    list->free = next; // This way we won't have any edge cases

    return SUCCESS();
}

template <typename E>
stack_trace* get_free_element(linked_list<E>* list, element_index_t* element_index) {
    *element_index = list->free;
    TRY get_free_element_on_place(list, list->free)
        FAIL("Can't detach list->free (%d) element!", list->free);
    return SUCCESS();
}

template <typename E>
void add_free_element(linked_list<E>* list, element_index_t element_index) {
    __linked_list_insert_after_in_place(list, (E) {}, list->free, element_index);
}

template <typename E>
static inline
bool is_free_element(linked_list<E>* list, element_index_t element_index) {
    return list->elements[element_index].is_free;
}


template <typename E>
static inline
void __linked_list_insert_after_in_place(linked_list<E>* list, E value,
                                         element_index_t prev_index,
                                         element_index_t place_for_new_element) {

    element<E>* prev_element = linked_list_get_pointer(list, prev_index);

    // Element that will go immediately after our new element
    element_index_t next_index = prev_element->next_index;
    element<E>* next_element = linked_list_get_pointer(list, next_index);

    //          next                        next          next
    // +------+ ~~~> +------+      +------x ~~~> /------x ~~~> /------+
    // | PREV | prev | NEXT |  =>  | PREV | prev | FREE | prev | NEXT |
    // +------+ <~~~ +------+      +------/ <~~~ x------/ <~~~ x------+

    // Update neighbours
    list->elements[prev_index].next_index = place_for_new_element;
    list->elements[next_index].prev_index = place_for_new_element;

    // Construct new element in the new place
    list->elements[place_for_new_element] = {
        .next_index = next_index,
        .prev_index = prev_index,
        .is_free = prev_element->is_free, .element = value
    };
}

template <typename E>
stack_trace* linked_list_insert_after(linked_list<E>* list, E value,
                                      element_index_t    prev_index,
                                      element_index_t* actual_index = NULL) {
    // Check if prev_index is valid index of list
    TRY check_index(list, prev_index) FAIL("Illegal index passed!");

    const double GROW = 2.0; // How much list grows when it runs out of space

    if (!free_elements_left(list))
        linked_list_resize(list, list->capacity * GROW);

    // Get free space for inserting new element
    element_index_t place_for_new_element = -1;
    if (prev_index <= list->capacity && is_free_element(list, prev_index + 1)) {
        place_for_new_element = prev_index + 1;

        TRY get_free_element_on_place(list, place_for_new_element)
            FAIL("Can't get desired free element!");
    } else {
        TRY get_free_element(list, &place_for_new_element)
            FAIL("Can't get free element!");

        list->is_linearized = false;
    }

    __linked_list_insert_after_in_place(list, value, prev_index,
                                        place_for_new_element);

    if (actual_index != NULL)
        *actual_index = place_for_new_element;

    ++ list->used; // This element was successfully added, let's update size

    return SUCCESS();
}

template <typename E>
inline stack_trace* linked_list_push_front(linked_list<E>* list, E element,
                                           element_index_t* actual_index = NULL) {

    // Inserting before head will result in pushing element to front
    return linked_list_insert_after(list, element, linked_list_end_index, actual_index);
}

template <typename E>
inline stack_trace* linked_list_push_back(linked_list<E>* list, E element,
                                          element_index_t* actual_index = NULL) {
    // Inserting after tail will result in pushing element to back
    return linked_list_insert_after(list, element,
                linked_list_tail_index(list), actual_index);
}


template <typename E>
stack_trace* linked_list_unlink(linked_list<E>* list, element_index_t actual_index) {
    // Check if element has is valid index in the list
    TRY check_index(list, actual_index) FAIL("Illegal index passed!");

    element<E>* current = &list->elements[actual_index];
    element_index_t prev_index = current->prev_index,
                    next_index = current->next_index;

    //          next          next                        next
    // +------x ~~~> /------x ~~~> /------+      +------+ ~~~> +------+
    // | PREV | prev | CURR | prev | NEXT |  =>  | PREV | prev | NEXT |
    // +------/ <~~~ x------/ <~~~ x------+      +------+ <~~~ +------+

    list->elements[prev_index].next_index = current->next_index;
    list->elements[next_index].prev_index = current->prev_index;

    return SUCCESS();
}

template <typename E>
stack_trace* linked_list_delete(linked_list<E>* list, element_index_t actual_index) {
    TRY check_index(list, actual_index) FAIL("Illegal index passed!");

    element<E>* current = linked_list_get_pointer(list, actual_index);
    const element_index_t head_ind = linked_list_head_index(list);

    if (current->next_index != head_ind && current->prev_index != head_ind)
        list->is_linearized = false;

    TRY linked_list_unlink(list, actual_index) FAIL("Cannot unlink element!");

    add_free_element(list, actual_index);

    -- list->used; // Element was successfully removed, let's correct size

    return SUCCESS();
}

template <typename E>
stack_trace* linked_list_pop_back(linked_list<E>* list) {
    return linked_list_delete(list, linked_list_head_index(list));
}

template <typename E>
stack_trace* linked_list_pop_front(linked_list<E>* list) {
    return linked_list_delete(list, linked_list_head_index(list));
}


template <typename E>
static inline void swap(E* first, E* second) {
    E temp = *first;
    *first = *second;
    *second = temp;
}

//  Swap physical positons of elements prev and next
//  without changing their logical order in a list. 
template <typename E>
stack_trace* linked_list_swap(linked_list<E>* list,
                              element_index_t fst_index,
                              element_index_t snd_index) {

    if (fst_index == snd_index)
        return SUCCESS();

    TRY check_index(list, fst_index) FAIL("Illegal second index!");

    TRY check_index(list, snd_index) FAIL("Illegal first index!");

    //           next   +-----+   prev
    //   (PREV1) ~~~>   | 1st |   <~~~ (NEXT1)
    //                  +-----+        
    //                 (fst_ind)

    //           next   +-----+   prev
    //   (PREV2) ~~~>   | 2nd |   <~~~ (NEXT2)
    //                  +-----+        
    //                 (snd_ind)

    element<E> *first    = linked_list_get_pointer(list, fst_index),
               *second   = linked_list_get_pointer(list, snd_index);

    element<E> *fst_prev = linked_list_prev(list,  first),
               *fst_next = linked_list_next(list,  first);

    element<E> *snd_prev = linked_list_prev(list, second),
               *snd_next = linked_list_next(list, second);

    // ==> Should become:
    //             next +-----+ prev
    // +~(PREV1)  +~~~> | 2nd | <~~~+  (NEXT1)~+
    // |          |     +-----+     |          |
    // |          |    (fst_ind)    |          |
    // |          |                 |          |
    // |          |     +-----+     |          |
    // | (PREV2)~~+     | 1st |     +~~(NEXT2) |
    // |          next  +-----+  prev          |
    // +~~~~~~~~~~~~~> (snd_ind) <~~~~~~~~~~~~~+

    fst_prev->next_index = fst_next->prev_index = snd_index;
    snd_prev->next_index = snd_next->prev_index = fst_index;

    swap(first, second); // We've prepared elements, now we can swap

    return SUCCESS();
}


#define LINKED_LIST_TRAVERSE(list, type, current)         \
    for (element<type>* current = linked_list_head(list); \
            current != linked_list_end (list);            \
            current  = linked_list_next(list, current))


template <typename E>
stack_trace* linked_list_linearize(linked_list<E>* list) {
    element_index_t logical_index = 1;
    for (element<E> *current =  linked_list_head(list);
            current != linked_list_end (list);
            current =  linked_list_next(list, current), ++ logical_index) {

        element_index_t actual_index = linked_list_get_index(list, current);

        TRY linked_list_swap(list, actual_index, logical_index)
            FAIL("Failed to exchange actual index with logical one!");

        current = linked_list_get_pointer(list, logical_index);
    }

    return SUCCESS();
}


template <typename E>
inline stack_trace* linked_list_get_logical_index(linked_list<E>*  const list,
                                                  const element_index_t logical_index,
                                                  element_index_t* const element_index) {
    if (list->is_linearized) {
        // Logical order starts from zero
        *element_index = linked_list_head_index(list) + logical_index;
        return SUCCESS();
    }

    element_index_t index = 0;
    LINKED_LIST_TRAVERSE(list, E, current)
        if (index ++ == logical_index) {
            *element_index = linked_list_get_index(list, current);
            return SUCCESS();
        }
}

template <typename E>
inline stack_trace* linked_list_get_logical(linked_list<E>* const list,
                                            const element_index_t logical_index,
                                            E* const value) {

    element_index_t actual_index = -1;
    TRY linked_list_get_logical_index(list, logical_index, &actual_index)
        FAIL("Can't get actual index of this element!");

    *value = list->elements[actual_index].element;

    return SUCCESS();
}


template <typename E>
void linked_list_destroy(linked_list<E> *list) {
    if (list != NULL) {
        free(list->elements);
        *list = {}; // Zero list out
    }

    // Do nothing if list is NULL (like free)
}

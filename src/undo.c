/**
 * @file undo.c
 * @brief Implementation of the multi-level undo/redo action history stack.
 */

#include "undo.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize an empty UndoStack.
 */
void undo_stack_init(UndoStack *stack) {
    if (!stack) return;
    stack->actions = NULL;
    stack->count = 0;
    stack->cap = 0;
}

/**
 * @brief Free all stored action payloads and reset stack count to zero.
 */
void undo_stack_clear(UndoStack *stack) {
    if (!stack) return;
    for (size_t i = 0; i < stack->count; ++i) {
        free(stack->actions[i].text);
        stack->actions[i].text = NULL;
    }
    stack->count = 0;
}

/**
 * @brief Free all payloads and release the backing action array.
 */
void undo_stack_free(UndoStack *stack) {
    if (!stack) return;
    undo_stack_clear(stack);
    free(stack->actions);
    stack->actions = NULL;
    stack->cap = 0;
}

/**
 * @brief Push a recorded buffer mutation onto the undo/redo stack.
 */
bool undo_stack_push(UndoStack *stack, ActionType type, size_t row, size_t col,
                     const char *text, size_t text_len, uint64_t group_id) {
    if (!stack) return false;

    // Expand array capacity exponentially if required
    if (stack->count >= stack->cap) {
        size_t new_cap = stack->cap == 0 ? 32 : stack->cap * 2;
        UndoAction *new_actions = (UndoAction*)realloc(stack->actions, new_cap * sizeof(UndoAction));
        if (!new_actions) return false;
        stack->actions = new_actions;
        stack->cap = new_cap;
    }

    UndoAction *act = &stack->actions[stack->count];
    act->type = type;
    act->row = row;
    act->col = col;
    act->group_id = group_id;
    act->text_len = text_len;

    if (text && text_len > 0) {
        act->text = (char*)malloc(text_len + 1);
        if (!act->text) return false;
        memcpy(act->text, text, text_len);
        act->text[text_len] = '\0';
    } else {
        act->text = NULL;
    }

    stack->count++;
    return true;
}

/**
 * @brief Pop the most recent action from the stack (transferring text ownership if out_action is non-NULL).
 */
bool undo_stack_pop(UndoStack *stack, UndoAction *out_action) {
    if (!stack || stack->count == 0) return false;
    stack->count--;
    if (out_action) {
        *out_action = stack->actions[stack->count];
    } else {
        free(stack->actions[stack->count].text);
    }
    return true;
}

/**
 * @brief Copy the top action into out_action without removing it from the stack.
 */
bool undo_stack_peek(const UndoStack *stack, UndoAction *out_action) {
    if (!stack || stack->count == 0 || !out_action) return false;
    *out_action = stack->actions[stack->count - 1];
    return true;
}

/**
 * @brief Return true if the stack has no recorded actions.
 */
bool undo_stack_is_empty(const UndoStack *stack) {
    return (!stack || stack->count == 0);
}

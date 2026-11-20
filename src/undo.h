/**
 * @file undo.h
 * @brief Multi-level undo and redo history tracking engine.
 *
 * Records buffer mutations (insertions, deletions, line splits, and joins)
 * in an action stack with group ID tracking so multi-character typing,
 * indentation, and composite formatting can be undone atomically.
 */

#ifndef MDE_UNDO_H
#define MDE_UNDO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Discrete mutation action types recorded in the history stack.
 */
typedef enum {
    ACTION_INSERT_TEXT,  /**< Text was inserted at (row, col) */
    ACTION_DELETE_TEXT,  /**< Text was removed from (row, col) */
    ACTION_SPLIT_LINE,   /**< Line was split at (row, col) via Enter */
    ACTION_JOIN_LINE     /**< Line row was merged with row+1 */
} ActionType;

/**
 * @brief A single recorded mutation action.
 */
typedef struct {
    ActionType type;     /**< Type of mutation */
    size_t row;          /**< 0-indexed row coordinate */
    size_t col;          /**< Byte offset coordinate */
    char *text;          /**< Copied text payload (for insert/delete) */
    size_t text_len;     /**< Length of payload in bytes */
    uint64_t group_id;   /**< Batch identifier for atomic multi-keystroke undo */
} UndoAction;

/**
 * @brief Dynamic array stack representing the undo or redo history.
 */
typedef struct {
    UndoAction *actions; /**< Array of recorded actions */
    size_t count;        /**< Number of actions currently on the stack */
    size_t cap;          /**< Allocated capacity of the array */
} UndoStack;

/**
 * @brief Initialize an empty undo/redo stack.
 * @param stack Pointer to UndoStack.
 */
void undo_stack_init(UndoStack *stack);

/**
 * @brief Free all actions and payloads stored in the stack.
 * @param stack Pointer to UndoStack.
 */
void undo_stack_free(UndoStack *stack);

/**
 * @brief Clear all actions from the stack without freeing the buffer capacity.
 * @param stack Pointer to UndoStack.
 */
void undo_stack_clear(UndoStack *stack);

/**
 * @brief Push a new mutation action onto the stack.
 *
 * @param stack The target UndoStack.
 * @param type The action type.
 * @param row The row coordinate.
 * @param col The byte offset.
 * @param text The text payload (copied internally).
 * @param text_len Length of text in bytes.
 * @param group_id Batch group identifier (0 for standalone).
 * @return true on success, false on allocation failure.
 */
bool undo_stack_push(UndoStack *stack, ActionType type, size_t row, size_t col,
                     const char *text, size_t text_len, uint64_t group_id);

/**
 * @brief Pop the top action from the stack.
 * @param stack The target UndoStack.
 * @param out_action Destination action struct (caller takes ownership of text payload).
 * @return true if popped successfully, false if stack was empty.
 */
bool undo_stack_pop(UndoStack *stack, UndoAction *out_action);

/**
 * @brief Peek at the top action without removing it.
 * @param stack The target UndoStack.
 * @param out_action Destination action struct.
 * @return true if an action was available, false if empty.
 */
bool undo_stack_peek(const UndoStack *stack, UndoAction *out_action);

/**
 * @brief Test whether the stack is empty.
 * @param stack Pointer to UndoStack.
 * @return true if empty, false otherwise.
 */
bool undo_stack_is_empty(const UndoStack *stack);

#ifdef __cplusplus
}
#endif

#endif // MDE_UNDO_H

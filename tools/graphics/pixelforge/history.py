"""Memento-based undo/redo history.

UndoStack is the caretaker: it stores opaque state snapshots (mementos)
produced by an originator (here: Canvas.snapshot()) and never inspects
them. Callers push the state captured *before* each mutation; undo() and
redo() exchange those snapshots against the caller's current state.

Coalescing: a burst of related mutations can share a coalesce_id; only the
first push of a run is recorded, so the whole burst undoes as one step.
"""


class UndoStack:
    """Bounded undo/redo stacks of (state, coalesce_id) entries."""

    def __init__(self, limit=200):
        self._limit = limit
        self._undo = []
        self._redo = []

    @property
    def can_undo(self):
        return bool(self._undo)

    @property
    def can_redo(self):
        return bool(self._redo)

    def push(self, state, coalesce_id=None):
        """Record the state taken before a mutation; invalidates redo."""
        self._redo.clear()
        if (
            coalesce_id is not None
            and self._undo
            and self._undo[-1][1] == coalesce_id
        ):
            return
        self._undo.append((state, coalesce_id))
        if len(self._undo) > self._limit:
            del self._undo[0]

    def undo(self, current_state):
        """Trade current_state for the previous one, or None if exhausted."""
        if not self._undo:
            return None
        state = self._undo.pop()[0]
        self._redo.append(current_state)
        return state

    def redo(self, current_state):
        """Trade current_state for the next one, or None if exhausted."""
        if not self._redo:
            return None
        state = self._redo.pop()
        self._undo.append((current_state, None))
        return state

    def clear(self):
        self._undo.clear()
        self._redo.clear()

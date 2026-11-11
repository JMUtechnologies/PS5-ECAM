# Named library collections

Collections work through the shared catalogue on every registered system,
including Switch 2. A collection name can be used on several systems; browsing
it shows matching games on the current side. Switching sides retains each
side's selected collection for the current launcher session.

## Add or remove a game

1. Focus a game and press Options.
2. Choose Collections.
3. Choose New collection, enter a name, and confirm the keyboard to add the game.
   To use an existing collection, select its name instead.
4. A check mark means this game belongs to the collection. Selecting a checked
   collection removes the game from it.

Names are limited to 96 UTF-8 bytes and a game can belong to up to 32 collections.
Empty names and control characters are rejected. A collection disappears from
selection when its last member is removed. Removing membership does not delete
games or saves.

## Browse

In Classic with library filters enabled, choose the collection's pill in the
filter row. In Orbit, or with filters disabled, move into the main Library list,
press Square, choose Collections, then choose a collection. The picker is offered
once this side has a collection.
The library heading shows its name. Search and the existing filters still apply;
hidden games remain hidden unless the Hidden filter is selected. Choose All in
the collection picker, or press Circle, to clear the collection selection.

Membership is kept in `library.json` alongside each game's existing metadata,
keyed by system and title ID. It survives restarts and rescans. A failed catalogue
write restores the previous in-memory membership and displays an error.

Upstream now includes named collections. Automatically populated genre/player-count
collections and a dedicated cross-system combined library are not included.

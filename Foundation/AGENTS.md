# Foundation project rules

- Restrict reads, analysis, edits and generated artifacts to this Foundation directory and its descendants. Do not inspect parent or sibling project directories. Read attachments explicitly supplied by the user when needed.
- Do not use `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr`, `std::make_unique`, `std::make_shared`, or replacement automatic owning pointer wrappers in first-party project code. Leave bundled third-party libraries such as JsonCpp and Excel unchanged; their internal smart pointer use is outside this rule's scope.
- Manage allocation, ownership transfer and destruction explicitly. Preserve cleanup on failure and exception paths; owning classes must not permit accidental copying.
- Handle concurrency explicitly with documented thread ownership, lock boundaries and producer shutdown. Pointer ownership or reference counts do not establish thread safety. Never assume derived destruction is synchronized by a base destructor.

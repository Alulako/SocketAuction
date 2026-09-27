# Development workflow

This project is intentionally built in small, testable increments.

## Rule for AI-assisted changes

For each change:

```text
1. Read the relevant files.
2. State one concrete objective.
3. Change only what is needed for that objective.
4. Compile with warnings enabled.
5. Run the relevant tests.
6. Review the diff and understand every change.
7. Commit only after the step works.
```

Avoid prompts such as "finish the whole project" or "improve everything". Prefer tasks such as:

```text
Implement only one-thread-per-client handling in server.c using pthreads.
Do not implement bidding yet and do not change the protocol.
Then compile and explain the changed functions.
```

## Quality gates

A milestone is complete only if:

- it compiles with `-Wall -Wextra -Wpedantic`;
- its tests pass;
- no unrelated functionality was changed;
- the team can explain the networking/concurrency concept involved;
- documentation is updated when protocol or architecture changes.

## Suggested commits

```text
feat: implement TCP server
feat: add client connection
feat: support multiple clients
feat: add user login
feat: add auction bidding
fix: protect auction state with mutex
feat: broadcast accepted bids
fix: handle client disconnect
 docs: document protocol and execution
```

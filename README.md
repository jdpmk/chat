# chat

A chat server which processes incoming client messages with thread-safe queues and a thread pool, using network sockets, POSIX threads (`pthread.h`), and an implementation of the concurrent producer-consumer pattern.

## Design

```
.----------.            .---------------------------------------------------------------------------.
|          |            |  recv()       server                                     server_  produce |
|          |            |      .----->  main  ------------> [sockets] <----------- poller `v        |
|        .-|   send()   |-.   /         thread     add                  select()   thread  pending  |
|        | |            | |--'                                                             sockets  |
| socket | | <--------> | |                                                                queue    |
|        | |            | |<-.        message    consume     pending    produce    client  _.^      |
|        '-|   recv()   |-'   `---- broadcaster ---------->  messages <----------  handler  consume |
|          |            |  send()     thread                 queue                 threads          |
'----------'            '---------------------------------------------------------------------------'

   Client                                                 Server
```


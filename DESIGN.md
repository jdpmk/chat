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

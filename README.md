# fsae

Event library extracted from Redis/Valkey

## USAGE
   In ONE .c/.cpp file, define the implementation macro before including:
      #define AE_IMPLEMENTATION
      #include "ae.h"
  In all other files, include normally (no macro needed):
        #include "ae.h"


## test

```sh
make examples
./fsae_example

echo "Hi there" | socat - TCP:localhost:8888
```

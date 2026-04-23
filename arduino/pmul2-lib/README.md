### Exemple d'utilisation

```cpp
#include <pmul2-lib.h>

Pmul2Lib objetPmul(Serial1);
Order currentOrder;
Color detected;

void setup() {
    Serial1.begin(9600);
}

void loop() {
    // On écoute pour voir si le raspberry détecte les commandes
    if (objetPmul.readBlockInfo(detected)) {
        currentOrder.addBox(detected);
        objetPmul.sendUpdateOrder(currentOrder);
    }
}
```

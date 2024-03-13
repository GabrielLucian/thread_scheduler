Scopul programului este de a simula cum functioneaza thread schedulerul sistemului de operare folosind Pthreads.

Programul foloseste 2 queue-uri pentru a salva threadurile gata de executie si cele in asteptarea unui semnal. Exista 2 structuri pentru thread si scheduler cu parametrii fiecaruia. 

Odata ce un thread este creat, ii se atribuie o cuanta de timp, un status si o prioritate. In functie de prioritate se decide daca acesta este pus in executie imediat sau bagat in queue-ul de ready ce este sortat dupa prioritate.
Simularea unei executii se face prin decrementarea cuantei de timp salvate in thread, dupa care se verifica daca mai continua executia sau ii ia locul alt thread in asteptare.

Un thread care ruleaza poate primi un semnal de wait cu un anumit dispozitiv, ceea ce rezulta in oprirea acestuia si bagarea in queue-ul de waiting. Acest thread poate fi trezit cu ajutorul unei functii care trezeste toate threadurile ce asteapta un anumit semnal, fapt ce rezulta in punerea acestuia in queue-ul de ready unde asteapta urmatoarea executie. 
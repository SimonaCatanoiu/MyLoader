# MyLoader

## Overview
<p align="justify">
Acest proiect este o tema de laborator pentru laboratorul de Proiectarea sistemelor de operare si reprezinta o implementare minmiala a unui loader de fisiere executabile in format ELF sub forma unei biblioteci partajate.
</p>

## Descriere implementare

Implementarea este realizata in fisierul ```loader.c``` si consta in realizarea mecanismului de tratare a exceptiilor pentru semnalul ```SIGSEGV``` corespunzator unui page fault. 


Pentru inceput, in functia ```so_execute``` vom deschide fisierul executabil pentru citire intr-un descriptor de fisiere static la nivelul programului, care ne va ajuta sa initializam paginile direct cu datele din fisier atunci cand vom face maparea.


Tot in functia ```so_execute``` vom aloca memorie pentru pointer-ul ```data``` din structura ```so_exec_t```,unde se va retine un vector de int-uri corespunzator numarului de pagini pentru fiecare segment (va retine 0 daca pagina nu a fost mapata si 1 daca s-a realizat maparea pentru pagina respectiva. La alocare, folosim ```calloc``` pentru a ne asigura ca vectorul are toate intrarile 0).


Functia ```ceiling``` are rolul de a rotunji superior la un numar intreg rezultatul unei impartiri. ( am folosit funtia pentru a determina cate pagini are fiecare segment)


In functia ```so_init_loader``` se va asigna un nou handler pentru ```SIGSEGV```. Implementarea handler-ului nou este functia ```segv_handler```, care realizata urmatoarele:


1. Verifica din ce segment face parte adresa care a generat page fault-ul. Daca adresa nu este corespunzatoare niciunui segment (nu se afla intre ```base_address``` si ```base_address + seg_size```), inseamna ca a avut loc un acces invalid la memorie si se va rula handler-ul implicit.


2. Daca apartine unui segment, verific daca page fault-ul a avut loc intr-o pagina deja mapata. (pentru a determina numarul paginii din segment, vom folosi functia ```get_page_index```). Daca pagina are in corespondentul sau din vectorul ```data``` al segmentului valoarea 1, atunci ea a fost deja mapata si insemna ca s-a facut un acces nepermis la memorie => se va rula handler-ul implicit.


3. Daca adresa care a generat page fault-ul apartine unei pagini care nu a fost inca mapata, atunci vom realiza maparea si copierea paginii respective direct din fisierul executabil(MAP_PRIVATE|MAP_FIXED) in functie de urmatoarele cazuri:


     - Cazul 1: dimensiunea segmentului in memorie = dimensiunea segmentului din fisier. In acest caz, vom mapa o pagina intreaga. (daca vrem sa mapam ultima pagina,va trebui sa precizam dimensiunea utila a ultimei pagini. Restul paginii va fi automat completat cu 0 conform ```mmap(2).```


     - Cazul 2: dimensiunea segmentului din memorie > dimensiunea segmentului din fisier. In acest caz, inseamna ca diferenta de bytes, va trebui zeroizata(paginile care se afla dincolo de dimensiunea fisierului, vor trebui mapate cu ```MAP_PRIVATE | MAP_FIXED | MAP_ANON``` pentru a asigura zeroizarea). In rest, maparea se va face ca la cazul 1, verificand unde anume in fisierul executabil trebuie mapata pagina.


4. La final, vom actualiza campul ```data``` pentru a marca faptul ca pagina a fost mapata.

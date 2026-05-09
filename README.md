# Koakuma

Un gestor de base de datos escrito en C++.

## Uso

### Usando Docker

El proyecto y su [frontend](https://github.com/project-sdm/koakuma-frontend) se
pueden levantar rápidamente usando Docker Compose:

```bash
docker compose up
```

Por defecto, esto levantará el DBMS en el puerto **8080** y el frontend en el puerto **3000**.

### Usando CMake

El proyecto se puede compilar y ejecutar con CMake de la siguiente forma:

```bash
cmake -S . -B build
cmake --build build
build/koakuma
```

# Procedimiento de Prueba para Bug Fix

## 🔄 Paso 1: Limpiar y Reinicializar el Disco

El disco puede tener archivos viejos con tamaño 0. Necesitamos reinicializarlo:

```bash
# Detener QEMU si está corriendo (Ctrl+C)

# Eliminar el disco viejo
rm disk.img

# Crear un disco nuevo
dd if=/dev/zero of=disk.img bs=1M count=10

# Reinicializar el filesystem MINIX en el disco
# (esto debería hacerse automáticamente al arrancar el kernel)
```

## 🧪 Paso 2: Procedimiento de Prueba Completo

1. **Arrancar el kernel:**
   ```bash
   make run
   ```

2. **En el shell del kernel, ejecutar en orden:**
   ```bash
   # Ver qué archivos existen (debería estar vacío o solo /)
   ls /
   
   # Escribir un archivo nuevo
   echo hola mundo > /test.txt
   
   # Verificar que se escribió
   cat /test.txt
   
   # Debería mostrar:
   # === File: /test.txt ===
   # hola mundo
   # --- End of file (11 bytes) ---
   ```

## 📊 Logs Esperados en Serial

### Al escribir con echo:
```
SERIAL: sys_write_file called
SERIAL: sys_write_file: pathname=0xXXXXXXXX content=0xXXXXXXXX
SERIAL: sys_write_file: calling minix_fs_write_file
SERIAL: minix_fs_write_file called
SERIAL: minix_fs_write_file: path=/test.txt content=hola mundo

SERIAL: write_file: before write_inode, inode_num=0xXX size=0x0000000B
SERIAL: write_inode: inode_num=0xXX block=0xXX offset=0xXX
SERIAL: write_inode: success
SERIAL: write_file: after write_inode
SERIAL: File '/test.txt' written successfully (0x0000000B bytes)
```

### Al leer con cat:
```
SERIAL: cat: inode_num=0xXX size=0x0000000B
```

## ⚠️ Problema Actual

El log muestra:
```
SERIAL: cat: inode_num=0x00000002 size=0x00000000
```

Esto significa:
- ✅ El archivo existe (inode #2)
- ❌ Pero tiene tamaño 0 bytes
- ❌ No se ve el log de `echo` escribiendo

### Posibles Causas:

1. **No ejecutaste `echo` antes de `cat`**
   - Solución: Ejecutar `echo texto > archivo.txt` primero

2. **El archivo ya existía en el disco viejo con tamaño 0**
   - Solución: Reinicializar el disco (ver Paso 1)

3. **El comando `echo` falló silenciosamente**
   - Verificar: Revisar si hay errores en pantalla
   - Verificar: Usar `touch /test.txt` primero

## 🔍 Debug Adicional

Si el problema persiste, prueba:

```bash
# En el shell del kernel:

# 1. Verificar que el filesystem está inicializado
ls /

# 2. Crear archivo vacío primero
touch /test.txt

# 3. Verificar que se creó
ls /

# 4. Escribir contenido
echo hola > /test.txt

# 5. Leer
cat /test.txt
```

## 📝 Notas

- El inode #1 es el directorio raíz `/`
- El inode #2 es el primer archivo creado
- Si ves `size=0x00000000`, el archivo existe pero está vacío
- Necesitas ver los logs de `minix_fs_write_file` para confirmar que se escribió

## ✅ Éxito Esperado

Cuando funcione correctamente, verás:
1. Logs de escritura en serial
2. Logs de lectura en serial con size > 0
3. Contenido del archivo en pantalla
4. "X bytes" al final (donde X > 0)

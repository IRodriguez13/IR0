# clean_all.make
.PHONY: clean_all

# Busca recursivamente todos los archivos .o y los elimina
clean_all:
	@echo "Limpiando todos los objetos (.o)..."
	@find . -type f -name '*.o' -exec rm -fv {} +
	@echo "Limpieza completada."
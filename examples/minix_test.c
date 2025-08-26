// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM STRUCTURE TEST
// ===============================================================================

#include <minix_fs.h>
#include <ir0/print.h>
#include <string.h>

void test_minix_inode_structure(void)
{
    print("\n=== MINIX INODE STRUCTURE TEST ===\n");

    // Test 1: Crear un inode de archivo regular
    print("1. Creating regular file inode...\n");
    minix_inode_t file_inode;
    memset(&file_inode, 0, sizeof(minix_inode_t));

    file_inode.i_mode = MINIX_IFREG | MINIX_IRUSR | MINIX_IWUSR | MINIX_IRGRP | MINIX_IROTH;
    file_inode.i_uid = 1000;
    file_inode.i_size = 1024;
    file_inode.i_time = 1234567890;
    file_inode.i_gid = 100;
    file_inode.i_nlinks = 1;

    // Configurar algunos bloques directos
    file_inode.i_zone[0] = 100;
    file_inode.i_zone[1] = 101;
    file_inode.i_zone[2] = 102;

    print("SUCCESS: Regular file inode created\n");

    // Test 2: Verificar tipo de archivo
    print("\n2. Testing file type detection...\n");
    if (minix_is_reg(&file_inode))
    {
        print("SUCCESS: File is recognized as regular file\n");
    }
    else
    {
        print("ERROR: File type detection failed\n");
        return;
    }

    if (!minix_is_dir(&file_inode))
    {
        print("SUCCESS: File is correctly not a directory\n");
    }
    else
    {
        print("ERROR: Directory detection failed\n");
        return;
    }

    // Test 3: Crear un inode de directorio
    print("\n3. Creating directory inode...\n");
    minix_inode_t dir_inode;
    memset(&dir_inode, 0, sizeof(minix_inode_t));

    dir_inode.i_mode = MINIX_IFDIR | MINIX_IRUSR | MINIX_IWUSR | MINIX_IXUSR | MINIX_IRGRP | MINIX_IXGRP | MINIX_IROTH | MINIX_IXOTH;
    dir_inode.i_uid = 1000;
    dir_inode.i_size = 2048;
    dir_inode.i_time = 1234567890;
    dir_inode.i_gid = 100;
    dir_inode.i_nlinks = 2;

    print("SUCCESS: Directory inode created\n");

    // Test 4: Verificar tipo de directorio
    print("\n4. Testing directory type detection...\n");
    if (minix_is_dir(&dir_inode))
    {
        print("SUCCESS: Directory is recognized correctly\n");
    }
    else
    {
        print("ERROR: Directory type detection failed\n");
        return;
    }

    if (!minix_is_reg(&dir_inode))
    {
        print("SUCCESS: Directory is correctly not a regular file\n");
    }
    else
    {
        print("ERROR: File type detection failed for directory\n");
        return;
    }

    // Test 5: Verificar permisos
    print("\n5. Testing permission detection...\n");
    uint16_t uid_perms = minix_get_uid_perms(&file_inode);
    uint16_t gid_perms = minix_get_gid_perms(&file_inode);
    uint16_t oth_perms = minix_get_oth_perms(&file_inode);

    print("User permissions: ");
    print_uint64(uid_perms);
    print(" (should be 6 for rw-)\n");

    print("Group permissions: ");
    print_uint64(gid_perms);
    print(" (should be 4 for r--)\n");

    print("Other permissions: ");
    print_uint64(oth_perms);
    print(" (should be 4 for r--)\n");

    if (uid_perms == 6 && gid_perms == 4 && oth_perms == 4)
    {
        print("SUCCESS: Permissions detected correctly\n");
    }
    else
    {
        print("ERROR: Permission detection failed\n");
        return;
    }

    // Test 6: Verificar tamaños de estructuras
    print("\n6. Testing structure sizes...\n");
    print("Inode size: ");
    print_uint64(sizeof(minix_inode_t));
    print(" bytes (should be 32)\n");

    print("Directory entry size: ");
    print_uint64(sizeof(minix_dir_entry_t));
    print(" bytes (should be 16)\n");

    print("Superblock size: ");
    print_uint64(sizeof(minix_superblock_t));
    print(" bytes\n");

    if (sizeof(minix_inode_t) == 32 && sizeof(minix_dir_entry_t) == 16)
    {
        print("SUCCESS: Structure sizes are correct\n");
    }
    else
    {
        print("ERROR: Structure sizes are incorrect\n");
        return;
    }

    // Test 7: Crear una entrada de directorio
    print("\n7. Creating directory entry...\n");
    minix_dir_entry_t dir_entry;
    dir_entry.inode = 1;
    strcpy(dir_entry.name, "test.txt");

    print("Directory entry created for inode ");
    print_uint64(dir_entry.inode);
    print(" with name: ");
    print(dir_entry.name);
    print("\n");

    print("SUCCESS: Directory entry created\n");

    print("\n=== MINIX INODE STRUCTURE TEST COMPLETED SUCCESSFULLY ===\n");
    print("All structures are working correctly!\n");
}

// Función para ser llamada desde el kernel
void run_minix_test(void)
{
    test_minix_inode_structure();
}

/*++

Module Name:

    dxboot.c

Abstract:

    This module finds the file that the boot command of the REX console
    names after the boot device, as in "boot 5/rz0/osloader".  The boot
    manager starts it at once, without the menu: the boot selection whose
    OS loader it is, or else the program, as Run a program finds it.  The
    command "boot 5/rz0" names no file, and the boot manager shows its menu.

Environment:

    Kernel mode only.

--*/

#include "fwp.h"
#include "string.h"


BOOLEAN
FwGetConsoleBootFile (
    OUT PCHAR FileName,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine returns the file that the console boot command names in
    the boot path, the argument with the device and its unit in it
    (slot/rzN/file), as a path name of the boot device: "5/rz0/osloader"
    gives "\osloader", "5/rz0/os/nt/osloader.exe" "\os\nt\osloader.exe".

Arguments:

    FileName - Supplies a pointer to the buffer that receives the path name,
        which is empty if the boot path names no file.

    Length - Supplies the length of the buffer.

Return Value:

    TRUE if the boot path names a file, FALSE otherwise.

--*/

{
    ULONG Index;
    ULONG Count;
    PCHAR Name;

    FileName[0] = 0;
    for (Index = 0; Index < FwRexArgc; Index++) {
        Name = strstr(FwRexArgv[Index], "/rz");
        if (Name != NULL) {

            //
            // The file follows the slash after the unit.
            //

            Name = strchr(Name + 1, '/');
            if ((Name == NULL) || (Name[1] == 0)) {
                return FALSE;
            }

            for (Count = 0; (Name[Count] != 0) && (Count < Length - 1); Count++) {
                FileName[Count] = (Name[Count] == '/') ? '\\' : Name[Count];
            }

            FileName[Count] = 0;
            return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN
FwMatchBootFile (
    IN PCHAR PathName,
    IN PCHAR FileName
    )

/*++

Routine Description:

    This routine tells if the console boot file names the program of a
    boot selection.  A file name that is a path ("\os\nt\osloader") names
    the program of that path, a file name alone ("\osloader") a program of
    that name in any directory.  The name may leave out the extension
    .exe.  Case does not matter.

Arguments:

    PathName - Supplies a pointer to the path name of the program of the
        boot selection, e.g. scsi()disk(0)rdisk()partition(1)\os\nt\osloader.exe.

    FileName - Supplies a pointer to the file name from FwGetConsoleBootFile.

Return Value:

    TRUE if the file name names the program, FALSE otherwise.

--*/

{
    PCHAR Name;
    ULONG Length;

    //
    // Take the path name on the device, and the last part of it if the
    // console names a file alone.
    //

    if ((Name = strrchr(PathName, ')')) != NULL) {
        PathName = Name + 1;
    }

    if ((strchr(FileName + 1, '\\') == NULL) &&
        ((Name = strrchr(PathName, '\\')) != NULL)) {
        PathName = Name;
    }

    if (*PathName == '\\') {
        PathName++;
    }

    if (*FileName == '\\') {
        FileName++;
    }

    Length = strlen(FileName);
    return (BOOLEAN)((_strnicmp(PathName, FileName, Length) == 0) &&
                     ((PathName[Length] == 0) ||
                      (_stricmp(&PathName[Length], ".exe") == 0)));
}


# collect up the source files
file(GLOB SRC_FILES "*.c" !vacuum.c)

# create the executable
add_library(commands OBJECT async.c creatinh.c command.c comment.c copy.c
    indexcmds.c define.c 
       remove.c rename.c  view.c cluster.c 
       explain.c sequence.c trigger.c user.c proclang.c 
       dbcommands.c variable.c)

add_dependencies(commands parser)
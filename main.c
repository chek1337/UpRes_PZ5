#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <signal.h>

#define SEM_KEY_1 123
#define SEM_KEY_2 456
#define SEM_KEY_3 768
#define SEM_KEY_4 910
#define SEM_KEY_5 1112

#define NAME "/tmp"
#define STRLEN 128
#define SIZE_SHMEM (STRLEN * sizeof(char))
#define NUM_CHILDPROCS 4

FILE *log;

volatile int stop = 0;

// Операция сигнала SIGUSR1 для отсановки считывания род процессом shmem 
void inthand(int signum)
{
    stop = 1;
}

// P-операция для закрытия семафора
void P(int semid)
{
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = -1;
    buf.sem_flg = SEM_UNDO;
    semop(semid, &buf, 1);
}

// V-операция для открытия семафора
void V(int semid)
{
    struct sembuf buf;
    buf.sem_num = 0;
    buf.sem_op = 1;
    buf.sem_flg = SEM_UNDO;
    semop(semid, &buf, 1);
}   

void Child(int childNum, int semRead, int semWrite)
{
    // Открывает объект shmem
    int fd = shm_open(NAME, O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("Child   : shm_open");
        return;
    }

    // Задание размера shmem
    ftruncate(fd, SIZE_SHMEM);

    // Cоздает новое сопоставление в виртуальном адресном пространстве вызывающего процесса
    char *data = (char *)mmap(0, SIZE_SHMEM, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    fprintf(stdout, "pid: %d\t sender mapped addr: %p\n", getpid(), data);
    
    // Открытие файла на считывание для взятия k-ых строчек
    FILE *fdPoem = fopen("poem.txt", "r");
   
    char strBuf[128];
    for (int i = 0; !feof(fdPoem); i++) 
    {
        fgets(strBuf, sizeof(strBuf), fdPoem); // Считывание строки из файла 
        if(i % NUM_CHILDPROCS == childNum) // Если текующая строка должна использоваться доч. процессом, то...
        {
            P(semWrite); // Ожиание разблокировки семафора
            snprintf(data, 150, "pid: %d\t msg: %s", getpid(), strBuf); // записали данные в shmem
            V(semRead); // Разблокировали семафора для считывания родительским процессом
        }
    }

    usleep(1000);

    for (int i = 0; i < 5; i++) // Послание сигнала родительскому процессу об окончании считывания файла дочерними процессами
        kill(getppid(), SIGUSR1);
    V(semRead);

    close(fd);
    fclose(fdPoem);
    shm_unlink(NAME);
}

void Parent(int semRead, int semWrite1, int semWrite2, int semWrite3, int semWrite4)
{
    signal(SIGUSR1, inthand);

    // Создает и открывает новый объект общей памяти
    int fd = shm_open(NAME, O_RDONLY, 0666);
    if (fd < 0) 
    {
        perror("Parent  : shm_open");
        return;
    }

    // Cоздает новое сопоставление в виртуальном адресном пространстве вызывающего процесса
    char *data = (char *)mmap(0, SIZE_SHMEM, PROT_READ, MAP_SHARED, fd, 0);
    fprintf(stdout, "pid: %d\t receiver mapped addr: %p\n", getpid(), data);

    int sems[NUM_CHILDPROCS];
    sems[0] =  semWrite1;
    sems[1] =  semWrite2;
    sems[2] =  semWrite3;
    sems[3] =  semWrite4;

    V(semWrite1); // Разблокировка семафора для P1

    for (int i = 1; ; i++)
    {
        P(semRead);
        if(stop != 1) // Ожиание разблокировки семафора
            fprintf(stdout, "%s", data);
        else
            break;
        V(sems[i % NUM_CHILDPROCS]); // Разблокировали семафор указанного дочернего процесса процессом
    }
    fprintf(stdout, "\n");
    
    close(fd);
    shm_unlink(NAME);
}

int main()
{
    log = fopen("log.txt", "w");

    // Создание семафора для считывания данных
    int semRead = semget(SEM_KEY_1, 1, IPC_CREAT | 0666);
    if (semRead == -1) {
        perror("semget");
        exit(1);
    }

    // Создание семафора для записи данных
    int semWrite1 = semget(SEM_KEY_2, 1, IPC_CREAT | 0666);
    if (semWrite1 == -1) {
        perror("semget");
        exit(1);
    }

    int semWrite2 = semget(SEM_KEY_3, 1, IPC_CREAT | 0666);
    if (semWrite2 == -1) {
        perror("semget");
        exit(1);
    }

    int semWrite3 = semget(SEM_KEY_4, 1, IPC_CREAT | 0666);
    if (semWrite3 == -1) {
        perror("semget");
        exit(1);
    }
    
    int semWrite4 = semget(SEM_KEY_5, 1, IPC_CREAT | 0666);
    if (semWrite4 == -1) {
        perror("semget");
        exit(1);
    }

    // Установка начальный значений семафоров
    if (semctl(semRead, 0, SETVAL, 0) == -1 || semctl(semWrite1, 0, SETVAL, 0) == -1 || 
        semctl(semWrite2, 0, SETVAL, 0) == -1 || semctl(semWrite3, 0, SETVAL, 0) == -1 ||
        semctl(semWrite4, 0, SETVAL, 0) == -1) 
    {
        perror("semctl");
        exit(1);
    }

    // Создает и открывает новый объект shmem
    int fdtest = shm_open(NAME, O_CREAT | O_EXCL | O_RDWR, 0600);

    pid_t pidP1, pidP2, pidP3, pidP4;
    switch (pidP1 = fork())
    {
    case -1:
        perror("fork");
        exit(EXIT_FAILURE);
    
    case 0:
        Child(0, semRead, semWrite1);
        exit(EXIT_SUCCESS);

    default:
        switch (pidP2 = fork())
        {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);

        case 0:
            Child(1, semRead, semWrite2);
            exit(EXIT_SUCCESS);

        default:
            switch (pidP3 = fork())
            {
            case -1:
            perror("fork");
            exit(EXIT_FAILURE);

            case 0:
                Child(2, semRead, semWrite3);
                exit(EXIT_SUCCESS);

            default:
                switch (pidP4 = fork())
                {
                case -1:
                    perror("fork");
                    exit(EXIT_FAILURE);

                case 0:
                    Child(3, semRead, semWrite4);
                    exit(EXIT_SUCCESS);
                
                default:
                    Parent(semRead, semWrite1, semWrite2, semWrite3, semWrite4);
                    for (int i = 0; i < NUM_CHILDPROCS; i++)
                        wait(NULL);
                    
                    exit(EXIT_SUCCESS);
                }
            }
        }
    }
    return 1;
}
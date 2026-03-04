#include <stdio.h> 
#include <pthread.h>
#include <stdbool.h>
#include <time.h> // 랜덤 시드 변경
#include <stdlib.h> // 랜덤 함수 사용 위한 헤더 
#include <unistd.h>
#include <termios.h> // 위의 3개는 입력 즉시 가능하게 해주는 헤더. 

#define MAX_WORKER_THREAD 3 // 잡 처리 스레드 갯수
#define MAX 100 // q 맥스 

typedef enum { TAKE, PUT, GIVE } RequestType; // 플레이어가 하는 요청 3개

typedef struct { RequestType type; int player_id; } Request; // 요청 큐에 들어갈 요청 형식

typedef enum { JOB_PUSH, JOB_POP } JobType; // 잡 스레드 풀의 스레드가 처리할 잡 2개

typedef struct { JobType type; int player_id; } Job;

Request request_queue[MAX]; // 요청 q
int req_q_front = 0, req_q_rear = 0;
pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER; // 요청 q 동시 점근 방지 락

Job job_queue[MAX]; // 잡 q
int job_q_front = 0, job_q_rear = 0;
pthread_mutex_t job_queue_mutex = PTHREAD_MUTEX_INITIALIZER;// 잡q 동시 접근 방지 락
pthread_cond_t job_queue_cond = PTHREAD_COND_INITIALIZER;// 잡 스레드 들 대기용 CV

// 카드 큐 및 상태 순환큐(원형큐)
int center_cards[MAX], center_front = 0, center_rear = 0;
int p1_cards[MAX], p1_front = 0, p1_rear = 0;
int p2_cards[MAX], p2_front = 0, p2_rear = 0;

int buffer = 0; // 카드 넣을 단일 버퍼. 카드에 들어갈 값은 하나. 0은 버퍼가 비어있다는 의미. 센터 카드 랜덤 값으로 0은 안나오게 제외함
bool buffer_full = false; // 카드 넣을 단일 버퍼 플래그. 카드에 들어갈 값은 하나.

// 버퍼, 센터, 플레이어 카드 리소스 중복 접근 방지 락
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_ready_cond = PTHREAD_COND_INITIALIZER; // pop 이루 push 하도록 필요한 CV 무전기

pthread_mutex_t center_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t p1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t p2_mutex = PTHREAD_MUTEX_INITIALIZER;

bool gameend = false; // 타이머 스레드가 시간 지나고 와일문 조건 바꿀 변수

//////////////////////////////////
int game_time = 30; // 게임 시간 조절
//////////////////////////////////

void enqueue_job(JobType type, int player_id) 
{
    pthread_mutex_lock(&job_queue_mutex); // 잡큐 락 잡기

    job_queue[job_q_rear++ % MAX] = (Job){type, player_id}; // 잡 넣기
    pthread_cond_signal(&job_queue_cond);// 잡 큐 시그널 보내서 잡 스레드 풀 에 있는 놈 한명 꺠우기

    pthread_mutex_unlock(&job_queue_mutex);
}

void handle_job(Job job) // 팝 = 버퍼에 카드 넣기 | 푸시 = 버퍼에서 카드 빼서 센터나 플레이어 카드에 넣기
{
    pthread_mutex_lock(&buffer_mutex); // 버퍼 락 잡기
    if (job.type == JOB_POP) 
    {
        int card = -1;
        if ( job.player_id == -1 ) 
        {
            pthread_mutex_lock(&center_mutex); // 센터 락 잡기
            card = card_pop(-1);
            pthread_mutex_unlock(&center_mutex);
        } 
        else if ( job.player_id == 0 ) 
        {
            pthread_mutex_lock(&p1_mutex); // p1 락 잡기
            card = card_pop(0);
            pthread_mutex_unlock(&p1_mutex);
        } 
        else if( job.player_id == 1 )
        {
            pthread_mutex_lock(&p2_mutex); // p2 락 잡기
            card = card_pop(1);
            pthread_mutex_unlock(&p2_mutex);
        }
        buffer = card;
        buffer_full = true;
        pthread_cond_signal(&buffer_ready_cond); // PUSH 가능 신호
    } 
    else if (job.type == JOB_PUSH) 
    {
        while (!buffer_full && gameend == false ) // 버퍼에 들어간 카드가 없고 게임 진행중이면 대기
        {
            pthread_cond_wait(&buffer_ready_cond, &buffer_mutex); 
            // POP 먼저 올 때까지 대기 자변서 버퍼 락 해제, 꺠면서 버퍼 락 잡기
        }
        if (gameend == true) // 게임 종료시 바로 리턴후 게임 종료
        {
            pthread_mutex_unlock(&buffer_mutex);
            return;
        }
        if(  buffer == 0 ) // 버퍼에 카드가 없으면
        {
            buffer_full = false; // 버퍼 상태 초기화
            pthread_mutex_unlock(&buffer_mutex); // 버퍼 락 해제
            return; // 아무 작업도 하지 않음
        }
        if ( job.player_id == -1 ) 
        {
            pthread_mutex_lock(&center_mutex); // 센터 락 잡기
            card_push(-1); // 센터 카드에 넣기
            pthread_mutex_unlock(&center_mutex);
        } 
        else if ( job.player_id == 0 ) 
        {
            pthread_mutex_lock(&p1_mutex); // p1 락 잡기
            card_push(0);
            pthread_mutex_unlock(&p1_mutex);
        } 
        else if(  job.player_id == 1)
        {
            pthread_mutex_lock(&p2_mutex); // p2 락 잡기
            card_push(1);
            pthread_mutex_unlock(&p2_mutex);
        }
        buffer = 0;
        buffer_full = false;
    }
    print_game_state(); // 게임 상태 출력
    pthread_mutex_unlock(&buffer_mutex); // 버퍼 락 해제
}

void* request_handler( void *arg ) // 플레이어가 보내주는 요청 q 처리
{
    while ( gameend == false ) // 게임 종료시까지 무한 반복
    {
        pthread_mutex_lock(&req_queue_mutex); // 일단 요청 q 접근시 락 잡기
        if (req_q_front != req_q_rear)// 요청 큐에 요청이 있음
        {
            Request req = request_queue[req_q_front++ % MAX];
            pthread_mutex_unlock(&req_queue_mutex); // 이 이후로는 요청 q 접근 안하므로 락 필요 없음
            switch (req.type) 
            { // -1 센터 
                case TAKE: // 센터에서 카드 가져오기
                    enqueue_job(JOB_POP, -1); // -1은 센터 카드에서 가져온다는 의미
                    enqueue_job(JOB_PUSH, req.player_id); 
                    break;
                case PUT: // 플레이어 카드 센터에 넣기
                    enqueue_job(JOB_POP, req.player_id);
                    enqueue_job(JOB_PUSH, -1);
                    break;
                case GIVE: // 플레이어 카드 다른 플레이어에게 주기
                    enqueue_job(JOB_POP, req.player_id);
                    enqueue_job(JOB_PUSH, 1 - req.player_id);
                    break; // 1 - req.player_id는 상대 플레이어의 id
            }
            // 디버깅용 어떤 요청이 들어왔는지 출력
            //printf("Request: %s from Player %d\n", (req.type == TAKE) ? "TAKE" : (req.type == PUT) ? "PUT" : "GIVE", req.player_id);
        } 
        else if( req_q_front == req_q_rear ) // 요청 큐가 비어있음
        {
            pthread_mutex_unlock(&req_queue_mutex); // 바로 락 풀기
        }
    }
    return NULL;
}

void* job_handler( void *arg ) // 잡 q 처리 스레드. 3개 존재
{
     while (  gameend == false ) // 게임 종료시까지 무한 반복
     {
        pthread_mutex_lock(&job_queue_mutex);
        while (job_q_front == job_q_rear && gameend == false ) // 잡 큐가 비어있고 게임 진행중이면 대기
        {
            pthread_cond_wait(&job_queue_cond, &job_queue_mutex);
        } // 인큐 잡
        if (gameend == true) // 게임 종료시 바로 리턴후 게임 종료
        {
            pthread_mutex_unlock(&job_queue_mutex);
            return NULL;
        }
        Job job = job_queue[job_q_front++ % MAX];
        pthread_mutex_unlock(&job_queue_mutex); // 이 이후로는 잡 q 접근 안하므로 락 필요 없음

        // 디버깅용 어떤 잡이 들어왔는지 출력
        //printf("Job: %s for Player %d\n", (job.type == JOB_PUSH) ? "PUSH" : "POP", job.player_id);
        handle_job(job); // 잡을 처리하는 함수. 
    }
}

void* timer_handler(void *arg) // 시간 딸각 스레드 시간 종료되면 자고 있는 다른 스레드 다 깨우고 게임 종료 화면 표시
{
    while (gameend == false && game_time >= 0) // 게임 종료시까지 무한 반복
    {
        sleep(1); // 1초마다
        game_time--; // 시간 감소
        print_game_state(); // 게임 상태 출력
    }
    gameend = true; // 게임 종료
    pthread_cond_broadcast(&job_queue_cond);    // 잡 큐 대기 스레드 모두 깨우기
    pthread_cond_broadcast(&buffer_ready_cond); // 버퍼 대기 스레드 모두 깨우기

    game_time = 0; // 시간 0으로 설정
    print_game_state(); // 마지막으로 게임 상태 출력

    // 플레이어 1, 2 총합 계산 및 출력
    int p1_sum = 0, p2_sum = 0;
    int i = p1_front;
    while (i != p1_rear) 
    {
        p1_sum += p1_cards[i % MAX];
        i = (i + 1) % MAX;
    }
    i = p2_front;
    while (i != p2_rear) 
    {
        p2_sum += p2_cards[i % MAX];
        i = (i + 1) % MAX;
    }
    printf("플레이어 1 총합: %d      ", p1_sum);
    printf("플레이어 2 총합: %d\n", p2_sum);

    if (p1_sum > p2_sum)
        printf("게임 종료!! 플레이어 1 승리!\n");
    else if (p2_sum > p1_sum)
        printf("게임 종료!! 플레이어 2 승리!\n");
    else
        printf("게임 종료!! 무승부!\n");

    printf("아무 키나 눌러서 게임을 종료해주세요!\n");
    return NULL;
}

int card_pop( int target ) 
{
    switch (target)
    {
        case -1:
            if (center_front == center_rear) return 0; // 센터 카드가 비어있음
            return center_cards[center_front++ % MAX]; // 센터 카드 반환
        case 0:
            if (p1_front == p1_rear) return 0; // 플레이어 1 카드가 비어있음
            return p1_cards[p1_front++ % MAX]; // 플레이어 1 카드 반환
        case 1:
            if (p2_front == p2_rear) return 0; // 플레이어 2 카드가 비어있음
            return p2_cards[p2_front++ % MAX]; // 플레이어 2 카드 반환
        default:
            return 0; // 잘못된 타겟
    }
    return 0; // 기본 반환값
}

void card_push( int target ) 
{
    switch (target)
    {
        case -1:
            if ( ( center_rear + 1 ) % MAX != center_front ) // 센터 카드가 꽉 차지 않았으면
            {
                center_cards[center_rear++ % MAX] = buffer; // 버퍼에 있는 카드 넣기
            }
            break;
        case 0:
            if ( ( p1_rear + 1 ) % MAX != p1_front ) // 플레이어 1 카드가 꽉 차지 않았으면
            {
                p1_cards[p1_rear++ % MAX] = buffer; // 버퍼에 있는 카드 넣기
            }
            break;
        case 1:
            if ( ( p2_rear + 1 ) % MAX != p2_front ) // 플레이어 2 카드가 꽉 차지 않았으면
            {
                p2_cards[p2_rear++ % MAX] = buffer; // 버퍼에 있는 카드 넣기
            }
            break;
        default:
            return; // 잘못된 타겟
    }
    return;
}

void set_center_cards() // 센터 카드 초기화
{
    for (int i = 0; i < 10; i++) // 센터 카드 10개
    {
        int card;
        do {
            card = (rand() % 21) - 10;
        } while (card == 0);
        center_cards[center_rear++ % MAX] = card; // 원형 큐 방식으로 추가
    }
}

void print_game_state() // 게임 화면 표시. 게임 시작시, 매초마다, 핸들 잡이 잡 하나 처리할 떄 마다, 끝날때
{
    system("clear");
    printf("=== GAME STATUS ===\n");
    printf("Time left: %d\n\n", game_time);

    printf("Player 1 Cards: ");
    int i = p1_front;
    while (i != p1_rear) 
    {
        printf("%d  ", p1_cards[i % MAX]);
        i = (i + 1) % MAX;
    }
    printf("\nP1: a(TAKE)  s(PUT)  d(GIVE)\n");
    printf("\n\n");

    printf("Center Cards: ");
    i = center_front;
    while (i != center_rear) 
    {
        printf("%d  ", center_cards[i % MAX]);
        i = (i + 1) % MAX;
    }
    printf("\n\n\n");

    printf("Player 2 Cards: ");
    i = p2_front;
    while (i != p2_rear) 
    {
        printf("%d  ", p2_cards[i % MAX]);
        i = (i + 1) % MAX;
    }
    printf("\nP2: j(TAKE)  k(PUT)  l(GIVE)\n");
    printf("\n");
}

void* player_input_thread(void* arg)// 각 플레이어 마다 입력 처리 받을 스레드
{
    char input;
    while (gameend == false) // 게임 종료시까지 무한 반복{
    {
        input = getchar(); // 엔터키 필요 없이 즉시 입력 모드로 인풋값 받기

        if( gameend == true ) return NULL; // 게임종료 되고 계속 입력 받는거 대기하고 있으므로 종료시 아무 키 입력하라 하고 종료

        Request req;
        if (input == 'a') req = (Request){TAKE, 0};
        else if (input == 's') req = (Request){PUT, 0};
        else if (input == 'd') req = (Request){GIVE, 0};
        else if (input == 'j') req = (Request){TAKE, 1};
        else if (input == 'k') req = (Request){PUT, 1};
        else if (input == 'l') req = (Request){GIVE, 1};
        else continue; // 잘못된 입력은 무시

        pthread_mutex_lock(&req_queue_mutex); // 요청 q 접근 시 락
        request_queue[req_q_rear++ % MAX] = req;
        // 디버깅용 어떤 키 입력했는지 출력
        //printf("Player %d: %s\n", req.player_id, (req.type == TAKE) ? "TAKE" : (req.type == PUT) ? "PUT" : "GIVE");
        
        pthread_mutex_unlock(&req_queue_mutex);
    }
    return NULL;
}

// 키 즉시 입력 코드
struct termios orig_termios;
void reset_terminal_mode() 
{
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_mode() 
{
    struct termios new_termios;
    tcgetattr(0, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &new_termios);
    atexit(reset_terminal_mode);
}

int main() 
{    
    ///////////////// 스레드 변수 선언
    pthread_t request_thread; // 요청 처리 스레드 1개
    pthread_t job_thread[MAX_WORKER_THREAD]; // 잡 처리 스레드 풀 -> 일단 3개
    pthread_t player_thread; // 플레이어 입력 처리 스레드 1개
    pthread_t timer_thread; // 타이머 스레드 1개
    ////////////////////////////////////////////////////////

    pthread_create( &request_thread, NULL, request_handler, NULL );// 스레드 생성
    for (int i = 0; i < MAX_WORKER_THREAD; i++) 
    {
        pthread_create( &job_thread[i], NULL, job_handler, NULL );// 스레드 반복 생성
    }
  
    pthread_create(&player_thread, NULL, player_input_thread, NULL ); // 플레이어 입력 처리 스레드 생성
 
    pthread_create( &timer_thread, NULL, timer_handler, NULL );
    // 디버깅 용 1 출력
    //printf("디버깅 용 1\n");

    set_conio_mode(); // 즉시 입력 모드 설정 made by GPT
    //gameend = true;

    srand(time(NULL)); // 랜덤 시드 설정
    set_center_cards(); // 센터 카드 초기화

    print_game_state(); // 맨 처음 게임 상태 센터 카드만 있는 거 출력
    // 이후에는 시간초가 1 감소때마다, 그리고 핸들 잡 에서 카드 상태 바꿀 떄 마다 화면 출력

    //////////////////////////////////////
    pthread_join(request_thread, NULL);
    for (int i = 0; i < MAX_WORKER_THREAD; i++) 
    {
        pthread_join(job_thread[i], NULL);
    }
    pthread_join(player_thread, NULL);
    pthread_join(timer_thread, NULL);
    /////////////////////////////////////
    return 0;
}

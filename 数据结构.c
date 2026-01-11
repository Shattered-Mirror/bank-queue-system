#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

// ==================== 常量和类型定义 ====================
#define MAX_CUSTOMERS 1000
#define MAX_WINDOWS 20
#define MAX_QUEUE_SIZE 1000
#define LOG_FILE_NAME "bank_simulation.log"

// 辅助函数：打印分隔线
void print_separator(int length, char ch) {
    for (int i = 0; i < length; i++) {
        printf("%c", ch);
    }
    printf("\n");
}

void fprint_separator(FILE* file, int length, char ch) {
    if (file == NULL) return;
    for (int i = 0; i < length; i++) {
        fprintf(file, "%c", ch);
    }
    fprintf(file, "\n");
}

// 客户结构体
typedef struct {
    int id;                 // 客户编号
    int type;               // 业务类型: 0-普通, 1-优先
    int vip_level;          // VIP等级: 0-普通, 1-银卡, 2-金卡, 3-钻石
    double arrival_time;    // 到达时间
    double service_time;    // 预估服务时长
    double start_time;      // 开始服务时间
    double finish_time;     // 完成时间
    double waiting_time;    // 等待时间
    int served_by;          // 服务窗口编号
} Customer;

// 窗口结构体
typedef struct {
    int id;                 // 窗口编号
    bool is_open;           // 是否开放
    bool is_busy;           // 是否忙碌
    Customer current_customer; // 当前服务的客户
    double busy_start;      // 开始忙碌时间
    double busy_end;        // 结束忙碌时间
    double total_busy_time; // 总忙碌时间
    double total_idle_time; // 总空闲时间
    int served_count;       // 已服务客户数
} Window;

// 队列节点
typedef struct Node {
    Customer customer;
    struct Node* next;
} Node;

// 队列结构
typedef struct {
    Node* front;            // 队首
    Node* rear;             // 队尾
    int size;               // 队列大小
    int priority;           // 队列优先级
} Queue;

// 仿真参数结构体
typedef struct {
    int initial_windows;    // 初始窗口数
    int max_windows;        // 最大窗口数
    int min_windows;        // 最小窗口数
    int open_threshold;     // 开窗阈值（队列长度）
    int close_threshold;    // 关窗阈值（队列长度）
    double priority_ratio;  // 优先业务服务比重
    int simulation_time;    // 仿真时间
    int customer_count;     // 客户总数
} SimulationParams;

// 统计结构体
typedef struct {
    double avg_wait_time[2];    // 平均等待时间[0普通,1优先]
    double max_wait_time[2];    // 最大等待时间[0普通,1优先]
    double window_utilization[MAX_WINDOWS];  // 窗口利用率
    double window_idle_rate[MAX_WINDOWS];    // 窗口空闲率
    int total_served;           // 总服务客户数
    double throughput;          // 系统吞吐量（客户/分钟）
    double total_wait_time[2];  // 总等待时间
    int served_count[2];        // 服务客户数
} Statistics;

// ==================== 全局变量 ====================
Queue priority_queue;      // 优先队列
Queue normal_queue;        // 普通队列
Window windows[MAX_WINDOWS]; // 窗口数组
SimulationParams params;   // 仿真参数
Statistics stats;          // 统计信息
Customer customers[MAX_CUSTOMERS]; // 客户数组
int active_windows;        // 当前活跃窗口数
double current_time;       // 当前仿真时间
int next_customer_id = 1;  // 下一个客户ID
bool log_events = true;    // 是否记录事件日志
FILE* log_file = NULL;     // 日志文件指针

// ==================== 队列操作函数 ====================
void init_queue(Queue* q, int priority) {
    q->front = q->rear = NULL;
    q->size = 0;
    q->priority = priority;
}

bool is_queue_empty(Queue* q) {
    return q->size == 0;
}

void enqueue(Queue* q, Customer customer) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->customer = customer;
    new_node->next = NULL;
    
    if (is_queue_empty(q)) {
        q->front = q->rear = new_node;
    } else {
        q->rear->next = new_node;
        q->rear = new_node;
    }
    q->size++;
}

Customer dequeue(Queue* q) {
    if (is_queue_empty(q)) {
        Customer empty = {0};
        return empty;
    }
    
    Node* temp = q->front;
    Customer customer = temp->customer;
    q->front = q->front->next;
    
    if (q->front == NULL) {
        q->rear = NULL;
    }
    
    free(temp);
    q->size--;
    return customer;
}

Customer peek_queue(Queue* q) {
    if (is_queue_empty(q)) {
        Customer empty = {0};
        return empty;
    }
    return q->front->customer;
}

int queue_size(Queue* q) {
    return q->size;
}

// ==================== 窗口管理函数 ====================
void init_windows() {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id = i;
        windows[i].is_open = (i < params.initial_windows);
        windows[i].is_busy = false;
        windows[i].total_busy_time = 0;
        windows[i].total_idle_time = 0;
        windows[i].served_count = 0;
        windows[i].busy_start = 0;
    }
    active_windows = params.initial_windows;
}

void open_window(int window_id) {
    if (window_id < MAX_WINDOWS && !windows[window_id].is_open && active_windows < params.max_windows) {
        windows[window_id].is_open = true;
        active_windows++;
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 窗口 %d 开放\n", current_time, window_id);
        }
        printf("时间 %.2f: 窗口 %d 开放\n", current_time, window_id);
    }
}

void close_window(int window_id) {
    if (window_id < MAX_WINDOWS && windows[window_id].is_open && 
        !windows[window_id].is_busy && active_windows > params.min_windows) {
        windows[window_id].is_open = false;
        active_windows--;
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 窗口 %d 关闭\n", current_time, window_id);
        }
        printf("时间 %.2f: 窗口 %d 关闭\n", current_time, window_id);
    }
}

int find_idle_window() {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].is_open && !windows[i].is_busy) {
            return i;
        }
    }
    return -1;
}

// ==================== 客户调度函数 ====================
Customer get_next_customer() {
    static int priority_counter = 0;
    Customer customer;
    
    // 根据优先业务比重调度
    if (!is_queue_empty(&priority_queue) && !is_queue_empty(&normal_queue)) {
        // 使用随机数决定从哪个队列取客户
        if ((rand() % 100) < (int)(params.priority_ratio * 100)) {
            customer = dequeue(&priority_queue);
        } else {
            customer = dequeue(&normal_queue);
        }
    } 
    // 如果只有一个队列有客户
    else if (!is_queue_empty(&priority_queue)) {
        customer = dequeue(&priority_queue);
    } 
    else if (!is_queue_empty(&normal_queue)) {
        customer = dequeue(&normal_queue);
    } 
    else {
        customer.id = -1; // 表示没有客户
    }
    
    return customer;
}

void assign_customer_to_window(int window_id, Customer customer) {
    if (window_id >= 0 && window_id < MAX_WINDOWS) {
        windows[window_id].is_busy = true;
        windows[window_id].current_customer = customer;
        windows[window_id].busy_start = current_time;
        windows[window_id].served_count++;
        
        // 更新客户信息
        customers[customer.id - 1].start_time = current_time;
        customers[customer.id - 1].waiting_time = current_time - customer.arrival_time;
        customers[customer.id - 1].served_by = window_id;
        
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 客户 %d (类型: %s) 在窗口 %d 开始服务，等待时间: %.2f\n", 
                   current_time, customer.id, 
                   customer.type == 1 ? "优先" : "普通",
                   window_id, current_time - customer.arrival_time);
        }
        printf("时间 %.2f: 客户 %d (类型: %s) 在窗口 %d 开始服务，等待时间: %.2f\n", 
               current_time, customer.id, 
               customer.type == 1 ? "优先" : "普通",
               window_id, current_time - customer.arrival_time);
    }
}

void finish_service(int window_id) {
    if (window_id >= 0 && window_id < MAX_WINDOWS && windows[window_id].is_busy) {
        Customer customer = windows[window_id].current_customer;
        double service_duration = current_time - windows[window_id].busy_start;
        
        windows[window_id].is_busy = false;
        windows[window_id].total_busy_time += service_duration;
        windows[window_id].busy_end = current_time;
        
        // 更新客户完成时间
        customers[customer.id - 1].finish_time = current_time;
        
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 客户 %d 在窗口 %d 完成服务，服务时长: %.2f\n", 
                   current_time, customer.id, window_id, service_duration);
        }
        printf("时间 %.2f: 客户 %d 在窗口 %d 完成服务，服务时长: %.2f\n", 
               current_time, customer.id, window_id, service_duration);
    }
}

// ==================== 动态窗口调整函数 ====================
void adjust_windows() {
    int total_queue_size = queue_size(&normal_queue) + queue_size(&priority_queue);
    
    // 开窗逻辑：队列长度超过阈值且还有窗口可以开
    if (total_queue_size > params.open_threshold) {
        for (int i = 0; i < params.max_windows; i++) {
            if (!windows[i].is_open) {
                open_window(i);
                break;
            }
        }
    }
    // 关窗逻辑：队列长度低于阈值且有空闲窗口可以关
    else if (total_queue_size < params.close_threshold) {
        for (int i = 0; i < params.max_windows; i++) {
            if (windows[i].is_open && !windows[i].is_busy) {
                close_window(i);
                break;
            }
        }
    }
}

// ==================== 客户到达函数 ====================
void customer_arrival(Customer customer) {
    // 将客户加入相应队列
    if (customer.type == 1) {
        enqueue(&priority_queue, customer);
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 优先客户 %d 到达，预估服务时间: %.2f\n", 
                   customer.arrival_time, customer.id, customer.service_time);
        }
        printf("时间 %.2f: 优先客户 %d 到达，预估服务时间: %.2f\n", 
               customer.arrival_time, customer.id, customer.service_time);
    } else {
        enqueue(&normal_queue, customer);
        if (log_events && log_file != NULL) {
            fprintf(log_file, "时间 %.2f: 普通客户 %d 到达，预估服务时间: %.2f\n", 
                   customer.arrival_time, customer.id, customer.service_time);
        }
        printf("时间 %.2f: 普通客户 %d 到达，预估服务时间: %.2f\n", 
               customer.arrival_time, customer.id, customer.service_time);
    }
    
    // 尝试分配客户到空闲窗口
    int idle_window = find_idle_window();
    if (idle_window != -1) {
        Customer next_customer = get_next_customer();
        if (next_customer.id != -1) {
            assign_customer_to_window(idle_window, next_customer);
        }
    }
    
    // 调整窗口数量
    adjust_windows();
}

// ==================== 仿真核心函数 ====================
void run_simulation() {
    init_windows();
    init_queue(&priority_queue, 1);
    init_queue(&normal_queue, 0);
    
    // 事件循环
    while (current_time < params.simulation_time) {
        // 查找下一个事件（客户到达或服务完成）
        double next_event_time = params.simulation_time + 1;
        int next_event_type = -1;
        int next_event_window = -1;
        int next_event_customer_index = -1;
        
        // 检查是否有客户到达
        for (int i = 0; i < params.customer_count; i++) {
            if (customers[i].arrival_time > current_time && 
                customers[i].arrival_time < next_event_time) {
                next_event_time = customers[i].arrival_time;
                next_event_type = 0; // 到达事件
                next_event_customer_index = i;
            }
        }
        
        // 检查是否有服务完成
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].is_busy) {
                double finish_time = windows[i].busy_start + 
                                   windows[i].current_customer.service_time;
                if (finish_time > current_time && finish_time < next_event_time) {
                    next_event_time = finish_time;
                    next_event_type = 1; // 完成事件
                    next_event_window = i;
                }
            }
        }
        
        if (next_event_type == -1) {
            // 没有更多事件，更新窗口空闲时间
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].is_open && !windows[i].is_busy) {
                    windows[i].total_idle_time += (params.simulation_time - current_time);
                }
            }
            current_time = params.simulation_time;
            break;
        }
        
        // 更新窗口空闲时间（从当前时间到下一个事件时间）
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].is_open && !windows[i].is_busy) {
                windows[i].total_idle_time += (next_event_time - current_time);
            }
        }
        
        // 更新时间
        current_time = next_event_time;
        
        // 处理事件
        if (next_event_type == 0) {
            // 客户到达
            customer_arrival(customers[next_event_customer_index]);
        } else if (next_event_type == 1) {
            // 服务完成
            finish_service(next_event_window);
            
            // 分配下一个客户
            Customer next_customer = get_next_customer();
            if (next_customer.id != -1) {
                assign_customer_to_window(next_event_window, next_customer);
            }
            
            // 调整窗口数量
            adjust_windows();
        }
    }
}

// ==================== 统计计算函数 ====================
void calculate_statistics() {
    // 初始化统计
    memset(&stats, 0, sizeof(Statistics));
    
    // 计算等待时间统计
    for (int i = 0; i < params.customer_count; i++) {
        if (customers[i].finish_time > 0) { // 已完成的客户
            int type = customers[i].type;
            stats.total_wait_time[type] += customers[i].waiting_time;
            stats.served_count[type]++;
            
            if (customers[i].waiting_time > stats.max_wait_time[type]) {
                stats.max_wait_time[type] = customers[i].waiting_time;
            }
        }
    }
    
    // 计算平均等待时间
    for (int i = 0; i < 2; i++) {
        if (stats.served_count[i] > 0) {
            stats.avg_wait_time[i] = stats.total_wait_time[i] / stats.served_count[i];
        }
    }
    
    // 计算窗口利用率
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].is_open) {
            double total_time = current_time;
            double total_used_time = windows[i].total_busy_time + windows[i].total_idle_time;
            
            if (total_used_time > 0) {
                stats.window_utilization[i] = (windows[i].total_busy_time / total_used_time) * 100;
                stats.window_idle_rate[i] = 100 - stats.window_utilization[i];
            } else {
                stats.window_utilization[i] = 0;
                stats.window_idle_rate[i] = 100;
            }
        }
    }
    
    // 计算总服务客户数和吞吐量
    stats.total_served = stats.served_count[0] + stats.served_count[1];
    if (current_time > 0) {
        stats.throughput = (stats.total_served / current_time) * 60; // 客户/小时
    }
}

// ==================== 输出函数 ====================
void print_statistics() {
    printf("\n");
    print_separator(45, '=');
    printf("仿真统计结果\n");
    print_separator(45, '=');
    
    printf("仿真时间: %.2f 分钟\n", current_time);
    printf("总服务客户数: %d\n", stats.total_served);
    printf("系统吞吐量: %.2f 客户/小时\n", stats.throughput);
    
    printf("\n--- 等待时间统计 ---\n");
    printf("普通客户: 平均等待 %.2f 分钟, 最长等待 %.2f 分钟, 服务 %d 人\n",
           stats.avg_wait_time[0], stats.max_wait_time[0], stats.served_count[0]);
    printf("优先客户: 平均等待 %.2f 分钟, 最长等待 %.2f 分钟, 服务 %d 人\n",
           stats.avg_wait_time[1], stats.max_wait_time[1], stats.served_count[1]);
    
    printf("\n--- 窗口利用率统计 ---\n");
    int open_window_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].is_open || windows[i].served_count > 0) {
            open_window_count++;
            printf("窗口 %d: 利用率 %.2f%%, 空闲率 %.2f%%, 服务客户数: %d\n",
                   i, stats.window_utilization[i], stats.window_idle_rate[i],
                   windows[i].served_count);
        }
    }
    printf("总计开放窗口数: %d\n", open_window_count);
    
    printf("\n--- 队列状态 ---\n");
    printf("优先队列剩余客户: %d\n", queue_size(&priority_queue));
    printf("普通队列剩余客户: %d\n", queue_size(&normal_queue));
    
    // 写入日志文件
    if (log_file != NULL) {
        fprintf(log_file, "\n");
        fprint_separator(log_file, 45, '=');
        fprintf(log_file, "仿真统计结果\n");
        fprint_separator(log_file, 45, '=');
        fprintf(log_file, "仿真时间: %.2f 分钟\n", current_time);
        fprintf(log_file, "总服务客户数: %d\n", stats.total_served);
        fprintf(log_file, "系统吞吐量: %.2f 客户/小时\n", stats.throughput);
    }
}

// ==================== 客户生成函数 ====================
void generate_customers_random(int count, int seed) {
    srand(seed);
    params.customer_count = count > MAX_CUSTOMERS ? MAX_CUSTOMERS : count;
    
    double arrival_rate = 2.0; // 平均每分钟到达2个客户
    double service_rate = 3.0; // 平均服务时间3分钟
    
    for (int i = 0; i < params.customer_count; i++) {
        customers[i].id = next_customer_id++;
        customers[i].type = (rand() % 100 < 30) ? 1 : 0; // 30%是优先客户
        customers[i].vip_level = customers[i].type == 1 ? (rand() % 3 + 1) : 0;
        
        // 指数分布生成到达间隔
        double random_value = (rand() % 9000 + 1000) / 10000.0; // 0.1-1.0之间的随机数
        double interarrival = -log(random_value) / arrival_rate;
        
        if (i == 0) {
            customers[i].arrival_time = interarrival;
        } else {
            customers[i].arrival_time = customers[i-1].arrival_time + interarrival;
        }
        
        // 指数分布生成服务时间
        random_value = (rand() % 9000 + 1000) / 10000.0; // 0.1-1.0之间的随机数
        customers[i].service_time = -log(random_value) / service_rate;
        
        // 限制服务时间范围
        if (customers[i].service_time < 0.5) customers[i].service_time = 0.5;
        if (customers[i].service_time > 10) customers[i].service_time = 10;
        
        customers[i].start_time = 0;
        customers[i].finish_time = 0;
        customers[i].waiting_time = 0;
        customers[i].served_by = -1;
    }
}

void generate_customers_from_input() {
    printf("请输入客户数量 (最大%d): ", MAX_CUSTOMERS);
    scanf("%d", &params.customer_count);
    
    if (params.customer_count > MAX_CUSTOMERS) {
        params.customer_count = MAX_CUSTOMERS;
        printf("警告：客户数量超过最大值，已自动调整为%d\n", MAX_CUSTOMERS);
    }
    
    printf("请按格式输入客户数据 (id type arrival_time service_time):\n");
    printf("示例: 1 1 0.0 3.5  (id=1, 优先客户, 到达时间0.0, 服务时间3.5分钟)\n");
    
    for (int i = 0; i < params.customer_count; i++) {
        printf("客户 %d: ", i+1);
        scanf("%d %d %lf %lf", 
              &customers[i].id, &customers[i].type,
              &customers[i].arrival_time, &customers[i].service_time);
        
        // 输入验证
        if (customers[i].type != 0 && customers[i].type != 1) {
            customers[i].type = 0;
        }
        if (customers[i].service_time <= 0) {
            customers[i].service_time = 1.0;
        }
        
        customers[i].vip_level = 0;
        customers[i].start_time = 0;
        customers[i].finish_time = 0;
        customers[i].waiting_time = 0;
        customers[i].served_by = -1;
        
        // 更新下一个客户ID
        if (customers[i].id >= next_customer_id) {
            next_customer_id = customers[i].id + 1;
        }
    }
}

// ==================== 参数设置函数 ====================
void set_default_parameters() {
    // 设置默认参数
    params.initial_windows = 3;
    params.max_windows = 5;
    params.min_windows = 2;
    params.open_threshold = 5;
    params.close_threshold = 2;
    params.priority_ratio = 0.7;
    params.simulation_time = 480; // 8小时
    params.customer_count = 50;
}

void set_custom_parameters() {
    printf("\n");
    print_separator(45, '=');
    printf("仿真参数设置\n");
    print_separator(45, '=');
    
    do {
        printf("初始窗口数 (1-%d): ", MAX_WINDOWS);
        scanf("%d", &params.initial_windows);
    } while (params.initial_windows < 1 || params.initial_windows > MAX_WINDOWS);
    
    do {
        printf("最大窗口数 (1-%d, 不小于初始窗口数): ", MAX_WINDOWS);
        scanf("%d", &params.max_windows);
    } while (params.max_windows < params.initial_windows || params.max_windows > MAX_WINDOWS);
    
    do {
        printf("最小窗口数 (1-%d, 不大于初始窗口数): ", MAX_WINDOWS);
        scanf("%d", &params.min_windows);
    } while (params.min_windows < 1 || params.min_windows > params.initial_windows);
    
    printf("开窗阈值 (队列长度, 建议3-10): ");
    scanf("%d", &params.open_threshold);
    
    printf("关窗阈值 (队列长度, 建议小于开窗阈值): ");
    scanf("%d", &params.close_threshold);
    
    do {
        printf("优先业务服务比重 (0.0-1.0): ");
        scanf("%lf", &params.priority_ratio);
    } while (params.priority_ratio < 0.0 || params.priority_ratio > 1.0);
    
    printf("仿真时间 (分钟, 建议60-1440): ");
    scanf("%d", &params.simulation_time);
    
    printf("记录事件日志? (1-是, 0-否): ");
    int log_choice;
    scanf("%d", &log_choice);
    log_events = (log_choice == 1);
}

// ==================== 演示模式函数 ====================
void demo_mode() {
    printf("\n");
    print_separator(50, '*');
    printf("进入演示模式，使用预设参数运行...\n");
    print_separator(50, '*');
    
    // 设置演示参数
    params.initial_windows = 2;
    params.max_windows = 4;
    params.min_windows = 1;
    params.open_threshold = 3;
    params.close_threshold = 1;
    params.priority_ratio = 0.7;
    params.simulation_time = 120; // 2小时
    log_events = true;
    
    // 生成演示客户数据
    generate_customers_random(20, 12345);
    
    printf("\n演示参数：\n");
    printf("初始窗口数: %d\n", params.initial_windows);
    printf("最大窗口数: %d\n", params.max_windows);
    printf("最小窗口数: %d\n", params.min_windows);
    printf("开窗阈值: %d\n", params.open_threshold);
    printf("关窗阈值: %d\n", params.close_threshold);
    printf("优先业务比重: %.1f\n", params.priority_ratio);
    printf("仿真时间: %d分钟\n", params.simulation_time);
    printf("客户总数: %d\n", params.customer_count);
    
    printf("\n按Enter键开始仿真演示...");
    getchar(); // 清除输入缓冲区
    getchar(); // 等待用户按Enter
    
    // 运行仿真
    printf("\n开始仿真演示...\n");
    current_time = 0;
    run_simulation();
    
    // 计算并输出统计
    calculate_statistics();
    print_statistics();
}

// ==================== 清空队列内存函数 ====================
void free_queue_memory(Queue* q) {
    while (!is_queue_empty(q)) {
        dequeue(q);
    }
}

// ==================== 模型对比函数 ====================
void model_comparison() {
    printf("\n");
    print_separator(50, '*');
    printf("三种排队模型对比测试\n");
    print_separator(50, '*');
    
    // 保存原始参数
    SimulationParams original_params = params;
    bool original_log_events = log_events;
    
    printf("\n测试使用相同的30个客户数据（随机种子1001）\n");
    
    // 测试1: 单队列单窗口模型
    printf("\n1. 单队列单窗口模型测试：\n");
    for (int i = 0; i < 40; i++) printf("-");
    printf("\n");
    
    params.initial_windows = 1;
    params.max_windows = 1;
    params.min_windows = 1;
    params.priority_ratio = 0.0; // 不使用优先级
    log_events = false; // 不记录详细日志
    
    generate_customers_random(30, 1001);
    current_time = 0;
    run_simulation();
    calculate_statistics();
    
    double single_avg_wait = (stats.avg_wait_time[0] + stats.avg_wait_time[1]) / 2;
    printf("平均等待时间: %.2f分钟\n", single_avg_wait);
    printf("吞吐量: %.2f客户/小时\n", stats.throughput);
    
    // 清空队列内存
    free_queue_memory(&priority_queue);
    free_queue_memory(&normal_queue);
    
    // 测试2: 多队列单窗口模型
    printf("\n2. 多队列单窗口模型测试：\n");
    for (int i = 0; i < 40; i++) printf("-");
    printf("\n");
    
    params.initial_windows = 1;
    params.max_windows = 1;
    params.min_windows = 1;
    params.priority_ratio = 0.7; // 启用优先级
    params.open_threshold = 10;  // 调高阈值，避免无效的窗口调整逻辑
    params.close_threshold = 5;
    
    generate_customers_random(30, 1001);
    current_time = 0;
    init_windows(); // 重新初始化窗口
    run_simulation();
    calculate_statistics();
    
    double multi_single_avg_wait = (stats.avg_wait_time[0] * stats.served_count[0] + 
                                   stats.avg_wait_time[1] * stats.served_count[1]) / 
                                   (stats.served_count[0] + stats.served_count[1]);
    printf("普通客户平均等待: %.2f分钟\n", stats.avg_wait_time[0]);
    printf("优先客户平均等待: %.2f分钟\n", stats.avg_wait_time[1]);
    printf("加权平均等待: %.2f分钟\n", multi_single_avg_wait);
    printf("吞吐量: %.2f客户/小时\n", stats.throughput);
    
    // 清空队列内存
    free_queue_memory(&priority_queue);
    free_queue_memory(&normal_queue);
    
    // 测试3: 多队列多窗口模型
    printf("\n3. 多队列多窗口模型测试：\n");
    for (int i = 0; i < 40; i++) printf("-");
    printf("\n");
    
    params = original_params; // 恢复原始参数
    log_events = original_log_events;
    
    generate_customers_random(30, 1001);
    current_time = 0;
    init_windows(); // 重新初始化窗口
    run_simulation();
    calculate_statistics();
    
    double multi_multi_avg_wait = (stats.avg_wait_time[0] * stats.served_count[0] + 
                                  stats.avg_wait_time[1] * stats.served_count[1]) / 
                                  (stats.served_count[0] + stats.served_count[1]);
    printf("普通客户平均等待: %.2f分钟\n", stats.avg_wait_time[0]);
    printf("优先客户平均等待: %.2f分钟\n", stats.avg_wait_time[1]);
    printf("加权平均等待: %.2f分钟\n", multi_multi_avg_wait);
    printf("吞吐量: %.2f客户/小时\n", stats.throughput);
    
    printf("\n模型对比总结：\n");
    printf("- 单队列单窗口：简单公平，但效率最低（平均等待: %.2f分钟）\n", single_avg_wait);
    printf("- 多队列单窗口：优先客户体验好，普通客户可能等待时间长（加权平均: %.2f分钟）\n", multi_single_avg_wait);
    printf("- 多队列多窗口：综合性能最好，资源利用率高（加权平均: %.2f分钟）\n", multi_multi_avg_wait);
}

// ==================== 主函数 ====================
int main() {
    printf("\n");
    print_separator(50, '=');
    printf("银行排队模拟系统\n");
    print_separator(50, '=');
    
    // 设置默认参数
    set_default_parameters();
    
    // 打开日志文件
    if (log_events) {
        log_file = fopen(LOG_FILE_NAME, "w");
        if (log_file == NULL) {
            printf("警告：无法创建日志文件，将只输出到屏幕\n");
            log_events = false;
        }
    }
    
    // 主菜单
    int main_choice;
    printf("\n请选择运行模式：\n");
    printf("1. 快速演示模式（使用预设参数）\n");
    printf("2. 自定义参数模式\n");
    printf("3. 三种模型对比测试\n");
    printf("4. 退出程序\n");
    printf("请选择 (1-4): ");
    scanf("%d", &main_choice);
    
    // 清除输入缓冲区
    while (getchar() != '\n');
    
    switch (main_choice) {
        case 1: // 演示模式
            demo_mode();
            break;
            
        case 2: // 自定义模式
            set_custom_parameters();
            
            printf("\n选择客户生成方式：\n");
            printf("1. 随机生成\n");
            printf("2. 手动输入\n");
            printf("请选择 (1-2): ");
            
            int data_choice;
            scanf("%d", &data_choice);
            
            if (data_choice == 1) {
                int customer_count, seed;
                printf("请输入客户数量 (最大%d): ", MAX_CUSTOMERS);
                scanf("%d", &customer_count);
                printf("请输入随机种子 (整数): ");
                scanf("%d", &seed);
                generate_customers_random(customer_count, seed);
            } else {
                generate_customers_from_input();
            }
            
            // 运行仿真
            printf("\n开始仿真...\n");
            if (log_events && log_file != NULL) {
                fprintf(log_file, "============= 仿真开始 =============\n");
            }
            
            current_time = 0;
            run_simulation();
            
            // 计算并输出统计
            calculate_statistics();
            print_statistics();
            break;
            
        case 3: // 三种模型对比测试
            model_comparison();
            break;
            
        case 4: // 退出
            printf("感谢使用，再见！\n");
            break;
            
        default:
            printf("无效选择，程序退出\n");
            break;
    }
    
    // 关闭日志文件
    if (log_file != NULL) {
        fclose(log_file);
        printf("\n详细日志已保存到 %s\n", LOG_FILE_NAME);
    }
    
    // 释放队列内存
    free_queue_memory(&priority_queue);
    free_queue_memory(&normal_queue);
    
    printf("\n按Enter键退出程序...");
    getchar(); // 等待用户按Enter
    
    return 0;
} 
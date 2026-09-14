# Документация API для продукта (PHP + MySQL PDO)

Полнофункциональный, модульный и защищенный REST-подобный бэкенд на PHP с использованием PDO MySQL. Подходит для десктопного ПО (C++, C#, Python, Rust), мобильных клиентов, лаунчеров и веб-панелей.

---

## 🚀 Быстрый запуск

1. **Создайте базу данных в MySQL:**
   - Импортируйте файл [`database.sql`](database.sql) через phpMyAdmin, DBeaver, HeidiSQL или консоль:
     ```sql
     mysql -u root -p < database.sql
     ```
2. **Настройте подключение в [`config.php`](config.php):**
   ```php
   define('DB_HOST', 'localhost');
   define('DB_NAME', 'my_product_db');
   define('DB_USER', 'root');
   define('DB_PASS', '');
   ```
3. **Запустите локальный сервер (например через OpenServer, XAMPP или встроенный PHP сервер):**
   ```bash
   php -S localhost:8000
   ```
4. **Тестируйте API прямо в браузере:**
   Откройте [`test_api.html`](test_api.html) в браузере для интерактивной отправки любых запросов и просмотра JSON-ответов.

---

## 🗄️ Схема базы данных

### Таблица `users`
| Колонка | Тип | Описание |
|---|---|---|
| `id` | `INT AUTO_INCREMENT` | Уникальный ID пользователя (PK) |
| `email` | `VARCHAR(255) UNIQUE` | Email адрес |
| `password` | `VARCHAR(255)` | Хэш пароля (`password_hash`, BCRYPT) |
| `hwid` | `VARCHAR(64)` | Hardware ID привязки железа (NULL до первой привязки) |
| `sub_id` | `INT` | ID текущего тарифного плана из таблицы `subs` |
| `sub_to` | `TIMESTAMP` | Дата и время окончания активной подписки |
| `banned` | `TINYINT(1)` | Статус бана (`0` — разбанен, `1` — забанен) |
| `dev` | `TINYINT(1)` | Права администратора/разработчика (`0` — обычный юзер, `1` — dev) |
| `balance` | `DOUBLE` | Баланс пользователя на счету (рубли/у.е., с копейками) |
| `token` | `VARCHAR(64)` | Сессионный токен доступа |
| `created_at`| `TIMESTAMP` | Дата регистрации |

### Таблица `subs`
| Колонка | Тип | Описание |
|---|---|---|
| `id` | `INT AUTO_INCREMENT` | Уникальный ID тарифа (PK) |
| `name` | `VARCHAR(255)` | Название тарифа (например, "Стандарт (30 дней)") |
| `sub_days` | `INT` | Количество дней действия |
| `cost` | `DOUBLE` | Базовая стоимость (с плавающей точкой / копейками) |
| `subs_value` | `INT` | Приоритет/уровень подписки |
| `discount` | `INT` | Размер скидки (в % при значении ≤ 100) |
| `discount_to` | `TIMESTAMP` | Время истечения скидки (NULL если бессрочно) |
| `created_at` | `TIMESTAMP` | Дата создания |

### Таблица `loader`
| Колонка | Тип | Описание |
|---|---|---|
| `id` | `INT AUTO_INCREMENT` | Уникальный ID записи (PK) |
| `ver` | `VARCHAR(32)` | Актуальная версия лоадера (например, "1.0.0") |

---

## 🔑 Начальные тестовые аккаунты (из `database.sql`)

- **Разработчик (dev = 1):**
  - Email: `admin@product.com`
  - Password: `admin123`
  - HWID: `HWID-ADMIN-TEST-PC-12345`
  - Баланс: 10 000
- **Пользователь (dev = 0):**
  - Email: `user@product.com`
  - Password: `user123`
  - HWID: `NULL` (привяжется автоматически при первом логине)
  - Баланс: 1 000

---

## 📡 Эндпоинты API

Все методы принимают параметры как в формате **JSON** (`Content-Type: application/json`), так и стандартный **POST/GET** (`x-www-form-urlencoded` / `multipart/form-data`).

---

### 1. Авторизация (`auth`)

Принимает `email`, `password` и `hwid`.  
- Если у пользователя `hwid` еще не задан — автоматически привязывает переданный `hwid`.
- Если `hwid` уже задан — сверяет его. При несовпадении отдает ошибку `HWID mismatch`.
- Отдает `true` и `id`, `sub_to`, `banned` (а также сессионный `token`, `dev` и статус активности подписки).

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "auth",
  "email": "user@product.com",
  "password": "user123",
  "hwid": "BFEBFBFF000906EA-UUID-987654321"
}
```

**Ответ при успехе:**
```json
{
  "success": true,
  "status": "success",
  "message": "Authentication successful.",
  "id": 2,
  "sub_to": "2026-10-12 21:00:00",
  "banned": 0,
  "token": "4a7c88b20d3f443b...",
  "dev": 0,
  "hwid": "BFEBFBFF000906EA-UUID-987654321",
  "sub_id": 2,
  "sub_active": true,
  "sub_days_left": 30,
  "balance": 1000
}
```

---

### 2. Регистрация (`reg`)

Регистрирует нового пользователя. Пароль автоматически хэшируется современным алгоритмом BCRYPT.

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "reg",
  "email": "newuser@example.com",
  "password": "strongPassword123",
  "hwid": "OPTIONAL_HWID"
}
```

**Ответ:**
```json
{
  "success": true,
  "status": "success",
  "message": "Registration successful.",
  "id": 3,
  "email": "newuser@example.com",
  "token": "..."
}
```

---

### 3. Покупка подписки (`sub_buy`)

Позволяет купить или продлить тарифный план.  
- Если у пользователя уже есть активная подписка, дни добавляются к текущей дате окончания (`sub_to`).
- Если подписка истекла или отсутствует, дата отсчитывается от текущего момента (`NOW() + N дней`).
- Автоматически учитывает активную скидку (`discount`).
- Проверяет баланс пользователя и списывает средства.

**Запрос:**
```json
POST /index.php
Content-Type: application/json
Authorization: Bearer 4a7c88b20d3f443b...

{
  "action": "sub_buy",
  "sub_id": 2
}
```

**Ответ:**
```json
{
  "success": true,
  "status": "success",
  "message": "Subscription purchased successfully.",
  "id": 2,
  "sub_id": 2,
  "sub_name": "Стандарт (30 дней)",
  "sub_days": 30,
  "sub_to": "2026-10-12 21:00:00",
  "cost_paid": 449,
  "balance": 551,
  "sub_active": true,
  "sub_days_left": 30
}
```

---

### 4. Бан пользователя (`ban`)

Устанавливает пользователю `banned = 1` и инвалидирует его сессионный токен.  
Доступно **только** разработчикам (`dev = 1`). Нельзя забанить другого разработчика.

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "ban",
  "admin_token": "YOUR_ADMIN_DEV_TOKEN",
  "target_id": 2,
  "reason": "Нарушение правил продукта"
}
```

**Ответ:**
```json
{
  "success": true,
  "status": "success",
  "message": "User has been banned successfully.",
  "target_id": 2,
  "target_email": "user@product.com",
  "banned": 1,
  "banned_by": 1
}
```

---

### 5. Разбан пользователя (`unban`)

Снимает бан с пользователя (`banned = 0`). Доступно только разработчикам (`dev = 1`).

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "unban",
  "admin_token": "YOUR_ADMIN_DEV_TOKEN",
  "target_id": 2
}
```

---

### 6. Сброс HWID (`hwid_reset`)

Сбрасывает привязку железа (`hwid = NULL`). При следующем входе пользователя новый HWID будет автоматически привязан.  
- Администратор может сбросить HWID любому пользователю, передав `target_id`.
- Обычный пользователь может сбросить свой собственный HWID.

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "hwid_reset",
  "token": "USER_OR_ADMIN_TOKEN",
  "target_id": 2
}
```

---

### 7. Список тарифов (`subs_list`)

Возвращает все тарифные планы с расчетом действующей цены со скидкой.

**Запрос:**
`GET /index.php?action=subs_list`

---

### 8. Профиль пользователя (`profile`)

Возвращает информацию о текущем пользователе, оставшихся днях подписки и балансе.

**Запрос:**
```json
POST /index.php
Authorization: Bearer <TOKEN>

{
  "action": "profile"
}
```

---

### 9. Пополнение баланса (`balance_add`)

Начисление баланса пользователю (для dev или мерчанта).

**Запрос:**
```json
POST /index.php
Content-Type: application/json

{
  "action": "balance_add",
  "admin_token": "YOUR_ADMIN_DEV_TOKEN",
  "target_id": 2,
  "amount": 500
}
```

---

### 10. Получение версии лоадера (`get_ver`)

Публичный метод для проверки актуальной версии лоадера. Запрашивает строку из таблицы `loader` по `id = 1` (или переданному параметру `id`) и возвращает версию из столбца `ver` (`VARCHAR`). Авторизация не требуется.

**Запрос (POST):**
```json
POST /index.php
Content-Type: application/json

{
  "action": "get_ver"
}
```

**Запрос (GET):**
```http
GET /index.php?action=get_ver
```
*(или `GET /get_ver` при настроенном mod_rewrite / clean URL)*

**Успешный ответ (200 OK):**
```json
{
  "success": true,
  "status": "success",
  "message": "Loader version retrieved successfully.",
  "id": 1,
  "ver": "1.0.0",
  "version": "1.0.0"
}
```

---

## 💻 Примеры интеграции в клиентский софт

### C++ (cURL / WinINet / CPR)
```cpp
// Пример запроса авторизации с передачей JSON
std::string payload = "{\"action\":\"auth\",\"email\":\"user@product.com\",\"password\":\"user123\",\"hwid\":\"" + get_hwid() + "\"}";
// Отправка POST запроса на http://your-server.com/index.php
```

### C# (.NET HttpClient)
```csharp
using var client = new HttpClient();
var payload = new {
    action = "auth",
    email = "user@product.com",
    password = "user123",
    hwid = HardwareId.Get()
};
var response = await client.PostAsJsonAsync("http://your-server.com/index.php", payload);
var result = await response.Content.ReadFromJsonAsync<AuthResponse>();
if (result.success && result.banned == 0 && result.sub_active) {
    // Доступ разрешен
}
```

### Python (requests)
```python
import requests

res = requests.post("http://localhost:8000/index.php", json={
    "action": "auth",
    "email": "user@product.com",
    "password": "user123",
    "hwid": "MY-HARDWARE-ID-12345"
}).json()

if res.get("success") and res.get("banned") == 0:
    print(f"Авторизован! Подписка до: {res.get('sub_to')}")
```

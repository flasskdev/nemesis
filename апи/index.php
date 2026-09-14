<?php
/**
 * Product API Entry Point & Router
 * Supports JSON payloads and standard POST/GET parameters
 */

// Enable CORS (Cross-Origin Resource Sharing)
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With');

// Handle preflight OPTIONS request
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

require_once __DIR__ . '/config.php';
require_once __DIR__ . '/db.php';
require_once __DIR__ . '/helpers.php';

$db = getDB();
$data = getRequestData();

// Determine requested action
$action = $data['action'] ?? $_GET['action'] ?? null;

// Support clean URLs if URL rewriting is enabled (e.g. /auth, /reg, /sub_buy)
if (empty($action)) {
    $uriPath = parse_url($_SERVER['REQUEST_URI'] ?? '', PHP_URL_PATH);
    $scriptName = $_SERVER['SCRIPT_NAME'] ?? '';
    $cleanPath = trim(str_replace([$scriptName, dirname($scriptName)], '', $uriPath), '/');
    if (!empty($cleanPath) && !preg_match('/\.php$/i', $cleanPath)) {
        $action = $cleanPath;
    }
}

if (empty($action)) {
    $action = 'help';
}

switch ($action) {

    // -------------------------------------------------------------
    // 1. REGISTRATION (reg)
    // -------------------------------------------------------------
    case 'reg':
    case 'register':
        $email = trim($data['email'] ?? '');
        $password = trim($data['password'] ?? '');
        $hwid = !empty($data['hwid']) ? trim($data['hwid']) : null;

        if (empty($email) || empty($password)) {
            jsonResponse(false, 'Email and password are required.', [], 400);
        }

        if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
            jsonResponse(false, 'Invalid email format.', [], 400);
        }

        if (mb_strlen($password) < 6) {
            jsonResponse(false, 'Password must be at least 6 characters long.', [], 400);
        }

        if ($hwid !== null && mb_strlen($hwid) > 64) {
            jsonResponse(false, 'HWID cannot exceed 64 characters.', [], 400);
        }

        // Check if user already exists
        $stmt = $db->prepare("SELECT id FROM users WHERE email = :email LIMIT 1");
        $stmt->execute([':email' => $email]);
        if ($stmt->fetch()) {
            jsonResponse(false, 'User with this email is already registered.', [], 409);
        }

        // Hash password securely with BCRYPT
        $passwordHash = password_hash($password, PASSWORD_BCRYPT);

        // Insert new user
        $insert = $db->prepare("
            INSERT INTO users (email, password, hwid, sub_id, sub_to, banned, dev, balance)
            VALUES (:email, :password, :hwid, NULL, NULL, 0, 0, 0)
        ");
        $insert->execute([
            ':email'    => $email,
            ':password' => $passwordHash,
            ':hwid'     => $hwid
        ]);

        $userId = (int)$db->lastInsertId();
        $token = generateAndStoreToken($db, $userId);

        jsonResponse(true, 'Registration successful.', [
            'id'    => $userId,
            'email' => $email,
            'hwid'  => $hwid,
            'token' => $token
        ], 201);
        break;

    // -------------------------------------------------------------
    // 2. AUTHENTICATION (auth)
    // -------------------------------------------------------------
    case 'auth':
    case 'login':
        $email = trim($data['email'] ?? '');
        $password = trim($data['password'] ?? '');
        $hwid = !empty($data['hwid']) ? trim($data['hwid']) : null;

        if (empty($email) || empty($password)) {
            jsonResponse(false, 'Email and password are required.', [], 400);
        }

        // Find user by email
        $stmt = $db->prepare("SELECT * FROM users WHERE email = :email LIMIT 1");
        $stmt->execute([':email' => $email]);
        $user = $stmt->fetch();

        if (!$user || !password_verify($password, $user['password'])) {
            jsonResponse(false, 'Invalid email or password.', [], 401);
        }

        $userId = (int)$user['id'];
        $userBanned = (int)$user['banned'];
        $userDev = (int)$user['dev'];
        $subTo = $user['sub_to'];
        $subId = $user['sub_id'] ? (int)$user['sub_id'] : null;

        // HWID Binding / Verification logic
        if (!empty($hwid)) {
            if (mb_strlen($hwid) > 64) {
                jsonResponse(false, 'HWID cannot exceed 64 characters.', [], 400);
            }

            if (empty($user['hwid'])) {
                // If HWID is not yet bound to user, automatically bind it
                if (AUTO_BIND_HWID_ON_FIRST_AUTH) {
                    $updateHwid = $db->prepare("UPDATE users SET hwid = :hwid WHERE id = :id");
                    $updateHwid->execute([':hwid' => $hwid, ':id' => $userId]);
                    $user['hwid'] = $hwid;
                }
            } else {
                // HWID is already set - check for match
                if ($user['hwid'] !== $hwid) {
                    jsonResponse(false, 'HWID mismatch! This account is linked to a different hardware.', [
                        'hwid_match' => false,
                        'id'         => $userId,
                        'banned'     => $userBanned
                    ], 403);
                }
            }
        }

        // Generate / refresh session token
        $token = generateAndStoreToken($db, $userId);

        // Check if subscription is currently active
        $isSubActive = false;
        $daysLeft = 0;
        if (!empty($subTo)) {
            $subTimestamp = strtotime($subTo);
            if ($subTimestamp !== false && $subTimestamp > time()) {
                $isSubActive = true;
                $daysLeft = (int)ceil(($subTimestamp - time()) / 86400);
            }
        }

        // Respond according to specifications: true and id, sub_to, banned + extras
        jsonResponse(true, 'Authentication successful.', [
            'id'            => $userId,
            'sub_to'        => $subTo,
            'banned'        => $userBanned,
            'token'         => $token,
            'dev'           => $userDev,
            'hwid'          => $user['hwid'],
            'sub_id'        => $subId,
            'sub_active'    => $isSubActive,
            'sub_days_left' => $daysLeft,
            'balance'       => (float)$user['balance']
        ]);
        break;

    // -------------------------------------------------------------
    // 3. BUY SUBSCRIPTION (sub_buy)
    // -------------------------------------------------------------
    case 'sub_buy':
    case 'buy_sub':
        // Authenticate user by token or credentials
        $user = authenticateUser($db, $data, false);
        $userId = (int)$user['id'];

        $subId = isset($data['sub_id']) ? (int)$data['sub_id'] : 0;
        if ($subId <= 0) {
            jsonResponse(false, 'Valid sub_id is required.', [], 400);
        }

        // Retrieve subscription info
        $subStmt = $db->prepare("SELECT * FROM subs WHERE id = :id LIMIT 1");
        $subStmt->execute([':id' => $subId]);
        $sub = $subStmt->fetch();

        if (!$sub) {
            jsonResponse(false, 'Subscription plan with the given sub_id was not found.', [], 404);
        }

        $subDays = (int)$sub['sub_days'];
        $finalCost = calculateSubPrice($sub);

        // Check balance if required
        if (REQUIRE_BALANCE_FOR_SUB && (float)$user['balance'] < $finalCost) {
            jsonResponse(false, sprintf(
                'Insufficient balance. Plan cost is %.2f, your current balance is %.2f.',
                $finalCost,
                (float)$user['balance']
            ), [
                'required_balance' => $finalCost,
                'current_balance'  => (float)$user['balance']
            ], 402);
        }

        // Calculate new subscription expiration timestamp
        $now = time();
        $currentSubTo = $user['sub_to'];
        $baseTimestamp = $now;

        // If user already has an active subscription, stack the days onto the existing expiry
        if (!empty($currentSubTo)) {
            $existingTimestamp = strtotime($currentSubTo);
            if ($existingTimestamp !== false && $existingTimestamp > $now) {
                $baseTimestamp = $existingTimestamp;
            }
        }

        $newSubToTimestamp = $baseTimestamp + ($subDays * 86400);
        $newSubTo = date('Y-m-d H:i:s', $newSubToTimestamp);

        // Begin transaction to ensure balance and subscription update atomicity
        $db->beginTransaction();
        try {
            if (REQUIRE_BALANCE_FOR_SUB && $finalCost > 0) {
                $deductBalance = $db->prepare("UPDATE users SET balance = balance - :cost WHERE id = :id");
                $deductBalance->execute([':cost' => $finalCost, ':id' => $userId]);
            }

            $updateSub = $db->prepare("
                UPDATE users
                SET sub_id = :sub_id, sub_to = :sub_to
                WHERE id = :id
            ");
            $updateSub->execute([
                ':sub_id' => $subId,
                ':sub_to' => $newSubTo,
                ':id'     => $userId
            ]);

            $db->commit();
        } catch (Exception $e) {
            $db->rollBack();
            jsonResponse(false, 'Failed to complete subscription purchase: ' . $e->getMessage(), [], 500);
        }

        // Fetch updated balance
        $refreshedStmt = $db->prepare("SELECT balance FROM users WHERE id = :id LIMIT 1");
        $refreshedStmt->execute([':id' => $userId]);
        $newBalance = (float)($refreshedStmt->fetch()['balance'] ?? 0.0);

        jsonResponse(true, 'Subscription purchased successfully.', [
            'id'            => $userId,
            'sub_id'        => $subId,
            'sub_name'      => $sub['name'],
            'sub_days'      => $subDays,
            'sub_to'        => $newSubTo,
            'cost_paid'     => $finalCost,
            'balance'       => $newBalance,
            'sub_active'    => true,
            'sub_days_left' => (int)ceil(($newSubToTimestamp - time()) / 86400)
        ]);
        break;

    // -------------------------------------------------------------
    // 4. BAN USER (ban) - Requires dev = 1
    // -------------------------------------------------------------
    case 'ban':
        // Authenticate admin (must have dev = 1)
        $admin = authenticateUser($db, $data, true);

        $targetId = isset($data['target_id']) ? (int)$data['target_id'] : 0;
        $targetEmail = !empty($data['target_email']) ? trim($data['target_email']) : null;
        $reason = !empty($data['reason']) ? trim($data['reason']) : 'Violation of Terms';

        if ($targetId <= 0 && empty($targetEmail)) {
            jsonResponse(false, 'target_id or target_email is required to ban a user.', [], 400);
        }

        // Find target user
        if ($targetId > 0) {
            $stmt = $db->prepare("SELECT * FROM users WHERE id = :id LIMIT 1");
            $stmt->execute([':id' => $targetId]);
        } else {
            $stmt = $db->prepare("SELECT * FROM users WHERE email = :email LIMIT 1");
            $stmt->execute([':email' => $targetEmail]);
        }

        $targetUser = $stmt->fetch();
        if (!$targetUser) {
            jsonResponse(false, 'Target user not found.', [], 404);
        }

        if ((int)$targetUser['dev'] === 1) {
            jsonResponse(false, 'Cannot ban another developer/administrator.', [], 403);
        }

        // Set banned = 1 and invalidate token
        $banStmt = $db->prepare("UPDATE users SET banned = 1, token = NULL WHERE id = :id");
        $banStmt->execute([':id' => (int)$targetUser['id']]);

        jsonResponse(true, 'User has been banned successfully.', [
            'target_id'    => (int)$targetUser['id'],
            'target_email' => $targetUser['email'],
            'banned'       => 1,
            'reason'       => $reason,
            'banned_by'    => (int)$admin['id']
        ]);
        break;

    // -------------------------------------------------------------
    // 5. UNBAN USER (unban) - Requires dev = 1
    // -------------------------------------------------------------
    case 'unban':
        $admin = authenticateUser($db, $data, true);

        $targetId = isset($data['target_id']) ? (int)$data['target_id'] : 0;
        $targetEmail = !empty($data['target_email']) ? trim($data['target_email']) : null;

        if ($targetId <= 0 && empty($targetEmail)) {
            jsonResponse(false, 'target_id or target_email is required to unban a user.', [], 400);
        }

        if ($targetId > 0) {
            $stmt = $db->prepare("SELECT * FROM users WHERE id = :id LIMIT 1");
            $stmt->execute([':id' => $targetId]);
        } else {
            $stmt = $db->prepare("SELECT * FROM users WHERE email = :email LIMIT 1");
            $stmt->execute([':email' => $targetEmail]);
        }

        $targetUser = $stmt->fetch();
        if (!$targetUser) {
            jsonResponse(false, 'Target user not found.', [], 404);
        }

        $unbanStmt = $db->prepare("UPDATE users SET banned = 0 WHERE id = :id");
        $unbanStmt->execute([':id' => (int)$targetUser['id']]);

        jsonResponse(true, 'User has been unbanned successfully.', [
            'target_id'    => (int)$targetUser['id'],
            'target_email' => $targetUser['email'],
            'banned'       => 0,
            'unbanned_by'  => (int)$admin['id']
        ]);
        break;

    // -------------------------------------------------------------
    // 6. RESET HWID (hwid_reset)
    // -------------------------------------------------------------
    case 'hwid_reset':
    case 'reset_hwid':
        // Either dev can reset for any target user, OR user resets their own if allowed
        $user = authenticateUser($db, $data, false);
        $targetId = isset($data['target_id']) ? (int)$data['target_id'] : (int)$user['id'];

        // If targeting another user, sender must be dev
        if ($targetId !== (int)$user['id'] && (int)$user['dev'] !== 1) {
            jsonResponse(false, 'Only developers can reset HWID for other users.', [], 403);
        }

        $resetStmt = $db->prepare("UPDATE users SET hwid = NULL WHERE id = :id");
        $resetStmt->execute([':id' => $targetId]);

        jsonResponse(true, 'HWID has been reset successfully. It will be bound on next login.', [
            'target_id' => $targetId,
            'hwid'      => null
        ]);
        break;

    // -------------------------------------------------------------
    // 7. LIST SUBSCRIPTIONS (subs_list)
    // -------------------------------------------------------------
    case 'subs_list':
    case 'subs':
        $stmt = $db->query("SELECT * FROM subs ORDER BY cost ASC");
        $subs = $stmt->fetchAll();

        $result = [];
        $now = time();

        foreach ($subs as $sub) {
            $originalCost = (float)$sub['cost'];
            $finalCost = calculateSubPrice($sub);
            $hasActiveDiscount = ($finalCost < $originalCost);

            $result[] = [
                'id'                  => (int)$sub['id'],
                'name'                => $sub['name'],
                'sub_days'            => (int)$sub['sub_days'],
                'original_cost'       => $originalCost,
                'final_cost'          => $finalCost,
                'discount'            => (int)$sub['discount'],
                'discount_to'         => $sub['discount_to'],
                'discount_active'     => $hasActiveDiscount,
                'subs_value'          => (int)$sub['subs_value']
            ];
        }

        jsonResponse(true, 'Subscriptions list retrieved.', ['subs' => $result]);
        break;

    // -------------------------------------------------------------
    // 8. USER PROFILE / STATUS (profile / check_sub)
    // -------------------------------------------------------------
    case 'profile':
    case 'check_sub':
    case 'user_info':
        $user = authenticateUser($db, $data, false);
        jsonResponse(true, 'User profile retrieved.', [
            'user' => sanitizeUserData($user)
        ]);
        break;

    // -------------------------------------------------------------
    // 9. BALANCE TOPUP / ADD (balance_add) - Requires dev = 1
    // -------------------------------------------------------------
    case 'balance_add':
    case 'add_balance':
        $admin = authenticateUser($db, $data, true);

        $targetId = isset($data['target_id']) ? (int)$data['target_id'] : 0;
        $amount = isset($data['amount']) ? (float)$data['amount'] : 0.0;

        if ($targetId <= 0 || $amount <= 0) {
            jsonResponse(false, 'target_id and a positive amount are required.', [], 400);
        }

        $stmt = $db->prepare("SELECT id, balance, email FROM users WHERE id = :id LIMIT 1");
        $stmt->execute([':id' => $targetId]);
        $targetUser = $stmt->fetch();

        if (!$targetUser) {
            jsonResponse(false, 'Target user not found.', [], 404);
        }

        $update = $db->prepare("UPDATE users SET balance = balance + :amount WHERE id = :id");
        $update->execute([':amount' => $amount, ':id' => $targetId]);

        $newBalance = (float)$targetUser['balance'] + $amount;

        jsonResponse(true, "Balance increased by {$amount}.", [
            'target_id'    => $targetId,
            'target_email' => $targetUser['email'],
            'amount_added' => $amount,
            'new_balance'  => $newBalance
        ]);
        break;

    // -------------------------------------------------------------
    // 10. GET LOADER VERSION (get_ver)
    // -------------------------------------------------------------
    case 'get_ver':
    case 'ver':
    case 'version':
        // Retrieve loader version from table `loader` (defaults to id = 1)
        $loaderId = isset($data['id']) && (int)$data['id'] > 0 ? (int)$data['id'] : 1;

        try {
            $stmt = $db->prepare("SELECT id, ver FROM loader WHERE id = :id LIMIT 1");
            $stmt->execute([':id' => $loaderId]);
            $loader = $stmt->fetch();

            if (!$loader) {
                jsonResponse(false, "Loader version with id {$loaderId} not found.", [], 404);
            }

            jsonResponse(true, 'Loader version retrieved successfully.', [
                'id'      => (int)$loader['id'],
                'ver'     => (string)$loader['ver'],
                'version' => (string)$loader['ver']
            ]);
        } catch (PDOException $e) {
            jsonResponse(false, 'Failed to retrieve loader version: ' . $e->getMessage(), [], 500);
        }
        break;

    // Skin sync routes. Existing account/subscription routes remain unchanged.
    case 'skin_sync_push':
    case 'sync_push':
    case 'skin_sync_pull':
    case 'sync_pull':
    case 'skin_sync_users':
    case 'sync_users':
        require_once __DIR__ . '/skin_sync_api.php';
        handleSkinSync($db, (string)$action, $data);
        break;

// 11. API HELP / DOCUMENTATION
    // -------------------------------------------------------------
    case 'help':
    case 'ping':
    default:
        jsonResponse(true, 'Product API is running.', [
            'version' => '1.0.0',
            'endpoints' => [
                'reg'        => ['method' => 'POST', 'params' => ['email', 'password', 'hwid (optional)'], 'desc' => 'Register a new user account'],
                'auth'       => ['method' => 'POST', 'params' => ['email', 'password', 'hwid (required)'], 'desc' => 'Authenticate user, binds/verifies HWID, returns token & sub info'],
                'sub_buy'    => ['method' => 'POST', 'params' => ['token', 'sub_id'], 'desc' => 'Purchase or extend a subscription'],
                'ban'        => ['method' => 'POST', 'params' => ['admin_token', 'target_id | target_email', 'reason'], 'desc' => 'Ban a user (requires dev = 1)'],
                'unban'      => ['method' => 'POST', 'params' => ['admin_token', 'target_id | target_email'], 'desc' => 'Unban a user (requires dev = 1)'],
                'hwid_reset' => ['method' => 'POST', 'params' => ['token', 'target_id (optional for dev)'], 'desc' => 'Reset HWID binding'],
                'subs_list'  => ['method' => 'GET/POST', 'params' => [], 'desc' => 'Get all available subscription plans with discounts'],
                'profile'    => ['method' => 'GET/POST', 'params' => ['token'], 'desc' => 'Get current user status, subscription details and balance'],
                'balance_add'=> ['method' => 'POST', 'params' => ['admin_token', 'target_id', 'amount'], 'desc' => 'Top-up user balance (requires dev = 1)'],
                'get_ver'    => ['method' => 'GET/POST', 'params' => ['id (optional, default: 1)'], 'desc' => 'Get loader version (table loader, id 1, column ver)']
            ]
        ]);
        break;
}

<?php
/**
 * Helper utilities for request parsing, response formatting, authentication, and validation
 */

require_once __DIR__ . '/config.php';

/**
 * Send JSON response and terminate script
 */
function jsonResponse(bool $success, string $message, array $data = [], int $httpCode = 200): void {
    http_response_code($httpCode);
    header('Content-Type: application/json; charset=utf-8');

    $response = array_merge([
        'success' => $success,
        'status'  => $success ? 'success' : 'error',
        'message' => $message,
    ], $data);

    echo json_encode($response, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
    exit;
}

/**
 * Retrieve request data from JSON body, POST, or GET
 */
function getRequestData(): array {
    $contentType = $_SERVER['CONTENT_TYPE'] ?? '';
    $rawInput = file_get_contents('php://input');

    $data = [];

    // Parse JSON body if Content-Type contains application/json or input is valid JSON
    if (!empty($rawInput)) {
        $jsonData = json_decode($rawInput, true);
        if (is_array($jsonData)) {
            $data = $jsonData;
        }
    }

    // Merge with $_POST and $_GET (POST has priority over GET, JSON has priority over POST)
    $data = array_merge($_GET, $_POST, $data);

    return $data;
}

/**
 * Generate a secure 64-character hex token and store in user record
 */
function generateAndStoreToken(PDO $db, int $userId): string {
    $token = bin2hex(random_bytes(32)); // 64 chars
    $stmt = $db->prepare("UPDATE users SET token = :token WHERE id = :id");
    $stmt->execute([':token' => $token, ':id' => $userId]);
    return $token;
}

/**
 * Extract Bearer token from Authorization header or request data
 */
function extractToken(array $data): ?string {
    // 1. Check from request data (token or admin_token)
    if (!empty($data['token'])) {
        return trim($data['token']);
    }
    if (!empty($data['admin_token'])) {
        return trim($data['admin_token']);
    }

    // 2. Check HTTP Authorization header
    $authHeader = $_SERVER['HTTP_AUTHORIZATION'] ?? $_SERVER['REDIRECT_HTTP_AUTHORIZATION'] ?? '';
    if (!empty($authHeader) && preg_match('/Bearer\s+(\S+)/i', $authHeader, $matches)) {
        return $matches[1];
    }

    // 3. Check getallheaders() if available
    if (function_exists('getallheaders')) {
        $headers = getallheaders();
        $auth = $headers['Authorization'] ?? $headers['authorization'] ?? '';
        if (!empty($auth) && preg_match('/Bearer\s+(\S+)/i', $auth, $matches)) {
            return $matches[1];
        }
    }

    return null;
}

/**
 * Authenticate user by token OR credentials (email & password)
 * If $requiredDev is true, verifies that dev == 1
 */
function authenticateUser(PDO $db, array $data, bool $requiredDev = false): array {
    $user = null;
    $token = extractToken($data);

    if (!empty($token)) {
        $stmt = $db->prepare("SELECT * FROM users WHERE token = :token LIMIT 1");
        $stmt->execute([':token' => $token]);
        $user = $stmt->fetch();
    }

    // Fallback: check email & password if provided (or admin_email & admin_password)
    if (!$user) {
        $email = $data['admin_email'] ?? $data['email'] ?? null;
        $password = $data['admin_password'] ?? $data['password'] ?? null;

        if (!empty($email) && !empty($password)) {
            $stmt = $db->prepare("SELECT * FROM users WHERE email = :email LIMIT 1");
            $stmt->execute([':email' => trim($email)]);
            $candidate = $stmt->fetch();

            if ($candidate && password_verify($password, $candidate['password'])) {
                $user = $candidate;
            }
        }
    }

    if (!$user) {
        jsonResponse(false, 'Unauthorized. Please provide valid token or email/password.', [], 401);
    }

    if ((int)$user['banned'] === 1) {
        jsonResponse(false, 'Account is banned.', ['banned' => 1, 'id' => (int)$user['id']], 403);
    }

    if ($requiredDev && (int)$user['dev'] !== 1) {
        jsonResponse(false, 'Access denied. Developer / Administrator privileges required.', [
            'id'  => (int)$user['id'],
            'dev' => 0
        ], 403);
    }

    return $user;
}

/**
 * Calculate actual subscription cost taking active discount into account (float/double)
 */
function calculateSubPrice(array $sub): float {
    $cost = (float)$sub['cost'];
    $discount = (float)($sub['discount'] ?? 0);
    $discountTo = $sub['discount_to'] ?? null;

    if ($discount > 0) {
        $isDiscountActive = false;
        if (empty($discountTo)) {
            $isDiscountActive = true;
        } else {
            $discountTimestamp = strtotime($discountTo);
            if ($discountTimestamp !== false && $discountTimestamp > time()) {
                $isDiscountActive = true;
            }
        }

        if ($isDiscountActive) {
            // If discount is <= 100, treat as percentage, otherwise fixed reduction
            if ($discount <= 100) {
                $cost = round($cost * (1 - ($discount / 100)), 2);
            } else {
                $cost = max(0.0, round($cost - $discount, 2));
            }
        }
    }

    return max(0.0, round($cost, 2));
}

/**
 * Clean and safe user data for public JSON response (removes password hash)
 */
function sanitizeUserData(array $user): array {
    unset($user['password']);

    $now = time();
    $subActive = false;
    $daysLeft = 0;

    if (!empty($user['sub_to'])) {
        $subToTimestamp = strtotime($user['sub_to']);
        if ($subToTimestamp !== false && $subToTimestamp > time()) {
            $subActive = true;
            $daysLeft = (int)ceil(($subToTimestamp - $now) / 86400);
        }
    }

    $user['id'] = (int)$user['id'];
    $user['banned'] = (int)$user['banned'];
    $user['dev'] = (int)$user['dev'];
    $user['balance'] = (float)($user['balance'] ?? 0.0);
    $user['sub_id'] = $user['sub_id'] ? (int)$user['sub_id'] : null;
    $user['sub_active'] = $subActive;
    $user['sub_days_left'] = $daysLeft;

    return $user;
}

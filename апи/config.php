<?php
/**
 * Configuration file for API
 * Database credentials and environment settings
 */

// Prevent direct script execution if needed
defined('API_ACCESS') or define('API_ACCESS', true);

// Error reporting (set to 0 in production)
ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);

// Set default timezone
date_default_timezone_set('Europe/Moscow');

// Database Configuration
define('DB_HOST', 'mysql-flasskdev.alwaysdata.net');
define('DB_PORT', '3306');
define('DB_NAME', 'flasskdev_ok');
define('DB_USER', 'flasskdev');
define('DB_PASS', '31052019RoG+');
define('DB_CHARSET', 'utf8mb4');

// Security Key for JWT / Token signatures (change to a random string)
define('API_SECRET_KEY', '2b7a6e9f8c3d1051b4e7f6d2c8a901345bcdef678901234567890abcdef');

// System Settings
define('REQUIRE_BALANCE_FOR_SUB', true); // If true, sub_buy checks & deducts balance
define('AUTO_BIND_HWID_ON_FIRST_AUTH', true); // If true, first auth automatically binds HWID

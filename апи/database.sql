-- --------------------------------------------------------
-- Database Schema for Product API
-- Compatible with MySQL 5.7, 8.0+, MariaDB 10.3+
-- --------------------------------------------------------

CREATE DATABASE IF NOT EXISTS `flasskdev_ok` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `flasskdev_ok`;

-- --------------------------------------------------------
-- Table structure for table `users`
-- --------------------------------------------------------
DROP TABLE IF EXISTS `users`;
CREATE TABLE `users` (
  `id` INT AUTO_INCREMENT PRIMARY KEY,
  `email` VARCHAR(255) NOT NULL UNIQUE,
  `password` VARCHAR(255) NOT NULL,
  `hwid` VARCHAR(64) DEFAULT NULL,
  `sub_id` INT DEFAULT NULL,
  `sub_to` TIMESTAMP NULL DEFAULT NULL,
  `banned` TINYINT(1) NOT NULL DEFAULT 0,
  `dev` TINYINT(1) NOT NULL DEFAULT 0,
  `balance` DOUBLE NOT NULL DEFAULT 0.00,
  `token` VARCHAR(64) DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX `idx_email` (`email`),
  INDEX `idx_token` (`token`),
  INDEX `idx_hwid` (`hwid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- --------------------------------------------------------
-- Table structure for table `subs`
-- --------------------------------------------------------
DROP TABLE IF EXISTS `subs`;
CREATE TABLE `subs` (
  `id` INT AUTO_INCREMENT PRIMARY KEY,
  `name` VARCHAR(255) NOT NULL,
  `sub_days` INT NOT NULL,
  `cost` DOUBLE NOT NULL DEFAULT 0.00,
  `subs_value` INT NOT NULL DEFAULT 0,
  `discount` INT NOT NULL DEFAULT 0,
  `discount_to` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- --------------------------------------------------------
-- Sample Subscriptions / Plans
-- --------------------------------------------------------
INSERT INTO `subs` (`id`, `name`, `sub_days`, `cost`, `subs_value`, `discount`, `discount_to`) VALUES
(1, 'Тестовый (1 день)', 1, 49.99, 1, 0, NULL),
(2, 'Стандарт (30 дней)', 30, 499.00, 2, 10, DATE_ADD(NOW(), INTERVAL 30 DAY)),
(3, 'Квартальный (90 дней)', 90, 1199.50, 3, 20, DATE_ADD(NOW(), INTERVAL 30 DAY)),
(4, 'Навсегда (Lifetime)', 3650, 2999.00, 4, 0, NULL);

-- --------------------------------------------------------
-- Default Users:
-- Admin: email = admin@product.com | password = admin123 | dev = 1 | balance = 10000.00
-- User:  email = user@product.com  | password = user123  | dev = 0 | balance = 1000.00
-- --------------------------------------------------------
INSERT INTO `users` (`id`, `email`, `password`, `hwid`, `sub_id`, `sub_to`, `banned`, `dev`, `balance`) VALUES
(1, 'admin@product.com', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'HWID-ADMIN-TEST-PC-12345', 4, DATE_ADD(NOW(), INTERVAL 3650 DAY), 0, 1, 10000.00),
(2, 'user@product.com',  '$2y$10$TKh8H1.PfQx37YgCzwiKb.KjNyWgaHb9cbcoQgdIVFlYg7B77UdFm', NULL, NULL, NULL, 0, 0, 1000.00);

-- --------------------------------------------------------
-- Table structure for table `loader`
-- --------------------------------------------------------
DROP TABLE IF EXISTS `loader`;
CREATE TABLE `loader` (
  `id` INT AUTO_INCREMENT PRIMARY KEY,
  `ver` VARCHAR(32) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- --------------------------------------------------------
-- Sample Loader Version
-- --------------------------------------------------------
INSERT INTO `loader` (`id`, `ver`) VALUES
(1, '1.0.0');


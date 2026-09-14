<?php
/** Skin sync API v1.1. Loaded by index.php after db.php and helpers.php. */

function skinSyncSteamId($value): string {
    // Floats may already have lost low SteamID bits. Never round them silently.
    if (!is_string($value) && !is_int($value)) {
        throw new InvalidArgumentException('steam_id must be a decimal string.');
    }
    $id = trim((string)$value);
    if (!preg_match('/^[0-9]{17}$/D', $id) ||
        strcmp($id, '76561197960265729') < 0 || strcmp($id, '76561202255233023') > 0) {
        throw new InvalidArgumentException('A valid individual SteamID64 is required.');
    }
    return $id;
}

function skinSyncInteger($value, string $field, int $maximum = 2147483647): int {
    if (is_string($value) && preg_match('/^(0|[1-9][0-9]*)$/D', $value)) {
        if (strlen($value) > 10 || (float)$value > $maximum) {
            throw new InvalidArgumentException($field . ' is out of range.');
        }
        $value = (int)$value;
    }
    if (!is_int($value) || $value < 0 || $value > $maximum) {
        throw new InvalidArgumentException($field . ' must be a non-negative integer in range.');
    }
    return $value;
}

function skinSyncNormalizeSkins($value): string {
    if (is_string($value)) {
        $value = json_decode($value, true, 64, JSON_BIGINT_AS_STRING);
        if (json_last_error() !== JSON_ERROR_NONE) {
            throw new InvalidArgumentException('skin_data contains invalid JSON.');
        }
    }
    if (!is_array($value) || count($value) > 512) {
        throw new InvalidArgumentException('skin_data must be an object with at most 512 entries.');
    }
    $normalized = new stdClass();
    foreach ($value as $def => $skin) {
        $key = (string)$def;
        if (!preg_match('/^[1-9][0-9]{0,4}$/D', $key) || (int)$key > 32767 || !is_array($skin)) {
            throw new InvalidArgumentException('Invalid item definition or skin entry.');
        }
        $wear = $skin['w'] ?? 0.01;
        if ((!is_int($wear) && !is_float($wear)) || !is_finite((float)$wear) || $wear < 0 || $wear > 1) {
            throw new InvalidArgumentException('Skin wear must be a finite number between 0 and 1.');
        }
        $stattrak = $skin['t'] ?? false;
        if (!is_bool($stattrak)) {
            throw new InvalidArgumentException('Skin stattrak must be a JSON boolean.');
        }
        $normalized->{$key} = [
            'p' => skinSyncInteger($skin['p'] ?? 0, 'paint_kit_id'),
            'w' => (float)$wear,
            's' => skinSyncInteger($skin['s'] ?? 0, 'seed', 1000),
            't' => $stattrak,
            'c' => skinSyncInteger($skin['c'] ?? 0, 'stattrak_count')
        ];
    }
    $json = json_encode($normalized, JSON_UNESCAPED_UNICODE | JSON_PRESERVE_ZERO_FRACTION);
    if ($json === false) {
        throw new InvalidArgumentException('Cannot encode skin_data.');
    }
    return $json;
}

function handleSkinSync(PDO $db, string $action, array $data): void {
    $requestId = bin2hex(random_bytes(8));
    header('X-Request-ID: ' . $requestId);
    $canonical = [
        'sync_push' => 'skin_sync_push',
        'sync_pull' => 'skin_sync_pull',
        'sync_users' => 'skin_sync_users'
    ][$action] ?? $action;
    $meta = ['action' => $canonical, 'protocol_version' => 1, 'request_id' => $requestId];
    try {
        if ($canonical === 'skin_sync_push') {
            if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
                header('Allow: POST');
                jsonResponse(false, 'POST is required.', $meta, 405);
            }
            $id = skinSyncSteamId($data['steam_id'] ?? null);
            // Missing data must not silently erase an existing profile.
            if (!array_key_exists('skin_data', $data)) {
                throw new InvalidArgumentException('skin_data is required; use {} to clear skins explicitly.');
            }
            $skins = skinSyncNormalizeSkins($data['skin_data']);
            $music = skinSyncInteger($data['music_kit_id'] ?? 0, 'music_kit_id');
            $ct = skinSyncInteger($data['agent_ct'] ?? 0, 'agent_ct', 32767);
            $t = skinSyncInteger($data['agent_t'] ?? 0, 'agent_t', 32767);
            $stmt = $db->prepare('INSERT INTO user_skins
                (steam_id, skin_data, music_kit_id, agent_ct, agent_t, updated_at)
                VALUES (:id, :skins, :music, :ct, :t, NOW())
                ON DUPLICATE KEY UPDATE skin_data = VALUES(skin_data),
                music_kit_id = VALUES(music_kit_id), agent_ct = VALUES(agent_ct),
                agent_t = VALUES(agent_t), updated_at = NOW()');
            $stmt->execute([':id' => $id, ':skins' => $skins, ':music' => $music, ':ct' => $ct, ':t' => $t]);
            $check = $db->prepare('SELECT updated_at FROM user_skins WHERE steam_id = :id');
            $check->execute([':id' => $id]);
            $updated = $check->fetchColumn();
            if ($updated === false) {
                throw new RuntimeException('Inserted skin row not found.');
            }
            jsonResponse(true, 'Skins synced successfully.', array_merge($meta, [
                'steam_id' => $id, 'music_kit_id' => $music, 'agent_ct' => $ct,
                'agent_t' => $t, 'updated_at' => $updated
            ]));
        }
        if ($canonical === 'skin_sync_pull') {
            $ids = $data['steam_ids'] ?? (isset($data['steam_id']) ? [$data['steam_id']] : []);
            if (is_string($ids)) {
                $ids = trim($ids) === '' ? [] : explode(',', $ids);
            }
            if (!is_array($ids) || count($ids) > 256) {
                throw new InvalidArgumentException('steam_ids must contain at most 256 IDs.');
            }
            $ids = array_values(array_unique(array_map('skinSyncSteamId', $ids)));
            $users = new stdClass();
            if ($ids) {
                $marks = implode(',', array_fill(0, count($ids), '?'));
                $stmt = $db->prepare('SELECT CAST(steam_id AS CHAR) AS steam_id, skin_data,
                    music_kit_id, agent_ct, agent_t, updated_at FROM user_skins WHERE steam_id IN (' . $marks . ')');
                $stmt->execute($ids);
                foreach ($stmt->fetchAll(PDO::FETCH_ASSOC) as $row) {
                    // Decode as objects: {} must remain {}, not PHP's empty [].
                    $skins = json_decode($row['skin_data']);
                    if ($skins === [] || $skins === null) $skins = new stdClass();
                    if (!is_object($skins)) {
                        error_log('[skin-sync] request=' . $requestId . ' invalid stored skin object');
                        continue;
                    }
                    $id = (string)$row['steam_id'];
                    $users->{$id} = [
                        'steam_id' => $id, 'skins' => $skins,
                        'music_kit_id' => (int)$row['music_kit_id'],
                        'agent_ct' => (int)$row['agent_ct'], 'agent_t' => (int)$row['agent_t'],
                        'updated_at' => $row['updated_at']
                    ];
                }
            }
            // jsonResponse accepts an array; the nested map is deliberately an object.
            jsonResponse(true, 'Synced skins retrieved.', array_merge($meta, ['users' => $users]));
        }
        if ($canonical === 'skin_sync_users') {
            $stmt = $db->query('SELECT CAST(steam_id AS CHAR) FROM user_skins
                WHERE updated_at >= DATE_SUB(NOW(), INTERVAL 60 MINUTE) ORDER BY steam_id');
            jsonResponse(true, 'Active sync users retrieved.', array_merge($meta, [
                'users' => array_map('strval', $stmt->fetchAll(PDO::FETCH_COLUMN))
            ]));
        }
        jsonResponse(false, 'Unknown sync action.', $meta, 404);
    } catch (InvalidArgumentException $e) {
        jsonResponse(false, $e->getMessage(), array_merge($meta, ['error_code' => 'INVALID_SKIN_SYNC_INPUT']), 400);
    } catch (Throwable $e) {
        // Keep database details server-side, not in public HTML or JSON responses.
        error_log('[skin-sync] request=' . $requestId . ' action=' . $canonical . ' exception=' . get_class($e) . ' code=' . $e->getCode());
        jsonResponse(false, 'Skin sync storage failed. Check the server log using request_id.',
            array_merge($meta, ['error_code' => 'SKIN_SYNC_STORAGE_ERROR']), 500);
    }
}

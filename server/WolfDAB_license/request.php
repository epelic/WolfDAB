<?php
declare(strict_types=1);
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

function answer(bool $sent, string $message, int $status = 200): never {
    http_response_code($status);
    echo json_encode(['sent' => $sent, 'message' => $message], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}
if ($_SERVER['REQUEST_METHOD'] !== 'POST') answer(false, 'Method not allowed', 405);
$in = json_decode((string)file_get_contents('php://input'), true);
$installation = strtoupper(trim((string)($in['installationCode'] ?? '')));
$name = trim((string)($in['name'] ?? ''));
$email = trim((string)($in['email'] ?? ''));
$location = trim((string)($in['location'] ?? ''));
$language = (int)($in['language'] ?? 0);
if (!preg_match('/^[A-F0-9]{4}(?:-[A-F0-9]{4}){5}$/', $installation) || mb_strlen($name) < 2 || mb_strlen($name) > 120 || mb_strlen($location) < 3 || mb_strlen($location) > 160 || !filter_var($email, FILTER_VALIDATE_EMAIL) || strlen($email) > 190) answer(false, 'Invalid request', 400);

$rateDir = __DIR__ . '/data/requests';
if (!is_dir($rateDir) && !mkdir($rateDir, 0700, true)) answer(false, 'Service unavailable', 503);
$fingerprint = hash('sha256', ($_SERVER['REMOTE_ADDR'] ?? '') . '|' . $installation);
$rateFile = $rateDir . '/' . $fingerprint;
if (is_file($rateFile) && time() - (int)filemtime($rateFile) < 900) answer(false, 'Request already sent', 429);

$subject = 'WolfDAB - richiesta registrazione - ' . $installation;
$body = "Nuova richiesta di registrazione gratuita WolfDAB\n\nNome: {$name}\nEmail: {$email}\nLocalità: {$location}\nCodice installazione: {$installation}\nLingua: {$language}\nData UTC: " . gmdate('c') . "\n";
$safeReply = str_replace(["\r", "\n"], '', $email);
$headers = "From: WolfDAB <noreply@freewaves.it>\r\nReply-To: {$safeReply}\r\nContent-Type: text/plain; charset=UTF-8\r\n";
if (!mail('max@freewaves.it', $subject, $body, $headers)) answer(false, 'Mail delivery failed', 503);
touch($rateFile);
answer(true, 'Request sent');

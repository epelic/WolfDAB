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

function b64url(string $value): string {
    return rtrim(strtr(base64_encode($value), '+/', '-_'), '=');
}
$privateKeyFile = __DIR__ . '/data/wolfdab-private.pem';
$privateKeyPem = @file_get_contents($privateKeyFile);
if ($privateKeyPem === false) answer(false, 'Registration service unavailable', 503);
$privateKey = openssl_pkey_get_private($privateKeyPem);
if ($privateKey === false) answer(false, 'Registration service unavailable', 503);
$payload = 'WolfDAB|' . $installation;
$signature = '';
if (!openssl_sign($payload, $signature, $privateKey, OPENSSL_ALGO_SHA256)) answer(false, 'Registration service unavailable', 503);
$activationCode = 'WD1.' . b64url($payload) . '.' . b64url($signature);

$rateDir = __DIR__ . '/data/requests';
if (!is_dir($rateDir) && !mkdir($rateDir, 0700, true)) answer(false, 'Service unavailable', 503);
$fingerprint = hash('sha256', ($_SERVER['REMOTE_ADDR'] ?? '') . '|' . $installation);
$rateFile = $rateDir . '/' . $fingerprint;
if (is_file($rateFile) && time() - (int)filemtime($rateFile) < 900) answer(false, 'Request already sent', 429);

$messages = [
    0 => ['Il tuo codice di registrazione WolfDAB', "Grazie per aver registrato WolfDAB.\n\nCodice installazione: {$installation}\n\nCodice di registrazione:\n{$activationCode}\n\nCopia il codice nella finestra di registrazione di WolfDAB. La registrazione è gratuita e non ha scadenza.\n\nFreewaves.it - Emanuele Pelicioli\nmax@freewaves.it\n"],
    1 => ['Your WolfDAB registration code', "Thank you for registering WolfDAB.\n\nInstallation code: {$installation}\n\nRegistration code:\n{$activationCode}\n\nPaste the code into the WolfDAB registration window. Registration is free and does not expire.\n\nFreewaves.it - Emanuele Pelicioli\nmax@freewaves.it\n"],
    2 => ['Ihr WolfDAB-Registrierungscode', "Vielen Dank für die Registrierung von WolfDAB.\n\nInstallationscode: {$installation}\n\nRegistrierungscode:\n{$activationCode}\n\nFügen Sie den Code in das WolfDAB-Registrierungsfenster ein. Die Registrierung ist kostenlos und läuft nicht ab.\n\nFreewaves.it - Emanuele Pelicioli\nmax@freewaves.it\n"],
    3 => ["Votre code d'enregistrement WolfDAB", "Merci d'avoir enregistré WolfDAB.\n\nCode d'installation : {$installation}\n\nCode d'enregistrement :\n{$activationCode}\n\nCollez le code dans la fenêtre d'enregistrement de WolfDAB. L'enregistrement est gratuit et sans expiration.\n\nFreewaves.it - Emanuele Pelicioli\nmax@freewaves.it\n"]
];
$selected = $messages[$language] ?? $messages[1];
$subject = 'WolfDAB - nuova registrazione - ' . $installation;
$body = "Registrazione WolfDAB generata automaticamente\n\nNome: {$name}\nEmail: {$email}\nLocalità: {$location}\nCodice installazione: {$installation}\nCodice attivazione: {$activationCode}\nLingua: {$language}\nData UTC: " . gmdate('c') . "\n";
$safeReply = str_replace(["\r", "\n"], '', $email);
$userHeaders = "From: WolfDAB <noreply@freewaves.it>\r\nReply-To: max@freewaves.it\r\nContent-Type: text/plain; charset=UTF-8\r\n";
$adminHeaders = "From: WolfDAB <noreply@freewaves.it>\r\nReply-To: {$safeReply}\r\nContent-Type: text/plain; charset=UTF-8\r\n";
if (!mail($email, $selected[0], $selected[1], $userHeaders)) answer(false, 'Mail delivery failed', 503);
mail('max@freewaves.it', $subject, $body, $adminHeaders);
touch($rateFile);
answer(true, 'Registration code sent');

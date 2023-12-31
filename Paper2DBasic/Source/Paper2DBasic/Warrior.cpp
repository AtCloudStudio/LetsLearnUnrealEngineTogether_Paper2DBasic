#include "Warrior.h"

#include "ComboComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperZDAnimationComponent.h"
#include "PaperZDAnimInstance.h"
#include "SensorComponent.h"
#include "SpriteScaleComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetSystemLibrary.h"

AWarrior::AWarrior()
{
	JumpMaxHoldTime = 0.3f;
	bUseControllerRotationYaw = false;
	
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("Camera Boom"));
	SpringArmComponent->SetupAttachment(RootComponent);
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
	SpringArmComponent->SetRelativeRotation(FRotator(0.0f, -90.0f,  0.0f));
	SpringArmComponent->TargetArmLength = 900.0f;
	SpringArmComponent->bInheritYaw = false;
	SpringArmComponent->bEnableCameraLag = true;
	SpringArmComponent->CameraLagSpeed = 8.0f;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(SpringArmComponent);

	SensorComponent = CreateDefaultSubobject<USensorComponent>(TEXT("Sensor Component"));
	SensorComponent->SetupAttachment(GetSprite());

	ComboComponent = CreateDefaultSubobject<UComboComponent>(TEXT("Combo Component"));

	SpriteScaleComponent = CreateDefaultSubobject<USpriteScaleComponent>(TEXT("Sprite Scale Component"));
	
	GetSprite()->SetRelativeLocation(DefaultSpriteOffset);
	GetSprite()->SetRelativeScale3D(FVector(5.0f));

	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->SetCrouchedHalfHeight(50.0f);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->bCanWalkOffLedgesWhenCrouching = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 10000.0f, 0.0f);
	GetCharacterMovement()->GravityScale = DefaultGravityScale;
	GetCharacterMovement()->JumpZVelocity = 800.0f;
	GetCharacterMovement()->AirControl = 0.9f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 600.0f;
	GetCharacterMovement()->LedgeCheckThreshold = 100.0f;
}

#pragma region Lifecycle Events

void AWarrior::BeginPlay()
{
	Super::BeginPlay();

	CrouchedSpriteOffset = FVector(DefaultSpriteOffset.X, DefaultSpriteOffset.Y, CrouchedSpriteHeight);
}

void AWarrior::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsSliding)
	{
		AddMovementInput(GetActorForwardVector());

		return;
	}

	if (IsFalling() && WallSlideCheck())
	{
		if (SensorComponent->WallBlockSightLine())
		{
			WallSlide();
		}
		else
		{
			LedgeGrab();
		}

		return;
	}

	if ((IsWallSliding || IsLedgeHanging) && !WallSlideCheck())
	{
		IsWallSliding = false;
		IsLedgeHanging = false;
		GetCharacterMovement()->GravityScale = DefaultGravityScale;

		if (IsFalling())
		{
			JumpToAnimationNode(JumpToFallNodeName);
		}
	}
}

#pragma endregion

#pragma region Movement Functions

void AWarrior::LightAttack()
{
	if (bIsCrouched) return;
	
	IsAttacking = true;
	ComboComponent->ComboCheck(EComboInput::LightAttack);
}

void AWarrior::HeavyAttack()
{
	if (bIsCrouched) return;
	
	IsAttacking = true;
	ComboComponent->ComboCheck(EComboInput::HeavyAttack);
}

void AWarrior::ChargeAttack()
{
	if (!IsGrounded() || IsAttacking || bIsCrouched) return;

	JumpToAnimationNode(JumpToChargeAttackAnimNodeName);
	IsAttacking = true;
	IsCharging = true;
}

void AWarrior::ReleaseChargeAttack()
{
	if (!IsCharging) return;
		
	JumpToAnimationNode(JumpToChargeAttackReleaseAnimNodeName);
	IsCharging = false;
}

void AWarrior::Move(const float InputActionValue)
{
	HasMoveInput = true;
	
	if (bIsCrouched)
	{
		if (InputActionValue < 0.0f)
		{
			SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		}
		else if (InputActionValue > 0.0f)
		{
			SetActorRotation(FRotator(0.0f));
		}
		
		return;
	}

	if (IsAttacking || IsDashing) return;
	
	AddMovementInput(FVector::ForwardVector, InputActionValue);

	if (!RunAnimationTriggered && IsGrounded())
	{
		JumpToAnimationNode(JumpToRunNodeName);
		RunAnimationTriggered = true;
		IsAttacking = false;
	}
}

void AWarrior::StopMoving()
{
	HasMoveInput = false;
	RunAnimationTriggered = false;

	if (IsAttacking || !IsGrounded() || IsSliding || bIsCrouched) return;

	JumpToAnimationNode(JumpToIdleNodeName);
}

void AWarrior::Slide()
{
	IsSliding = true;
	JumpToAnimationNode(JumpToSlideNodeName);

	FTimerHandle SlideTimerHandle;
	GetWorldTimerManager().SetTimer(SlideTimerHandle, this, &AWarrior::StopSliding, SlideDuration);
}

void AWarrior::StopSliding()
{
	IsSliding = false;
	
	if (HasCrouchedInput || IsWallAbove())
	{
		if (IsFalling()) return;
		
		GetSprite()->SetRelativeLocation(CrouchedSpriteOffset);
		JumpToAnimationNode(JumpToCrouchingNodeName);
	}
	else
	{
		Super::UnCrouch();
		GetSprite()->SetRelativeLocation(DefaultSpriteOffset);
		JumpToAnimationNode(JumpToStopSlidingNodeName);
	}
}

void AWarrior::OnJumpInput()
{
	if (IsWallSliding || IsLedgeHanging)
	{
		WallJump();

		return;
	}
	
	if (IsAttacking || !IsGrounded() || IsDashing) return;
		
	if (bIsCrouched && !IsSliding)
	{
		Slide();

		return;
	}

	Jump();
}

void AWarrior::OnUpInput()
{
	if (IsLedgeHanging)
	{
		const float LedgeClimbingOffsetX = GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.5f * GetActorForwardVector().X;
		const float LedgeClimbingOffsetZ = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.5f;
		const FVector LedgeClimbingOffset = FVector(LedgeClimbingOffsetX, 0, LedgeClimbingOffsetZ);
		const FVector LedgeClimbingLocation = GetActorLocation() + LedgeClimbingOffset;

		FLatentActionInfo LatentActionInfo;
		LatentActionInfo.CallbackTarget = this;
		UKismetSystemLibrary::MoveComponentTo(
			GetRootComponent(),
			LedgeClimbingLocation,
			GetActorRotation(),
			true, true, 0.2f, false,
			EMoveComponentAction::Type::Move,
			LatentActionInfo);

		JumpToAnimationNode(JumpToJumpUpNodeName);
	}
}

void AWarrior::OnDownInputPressed()
{
	HasCrouchedInput = true;

	if (SensorComponent->AheadLedgeCheck())
	{
		FLatentActionInfo LatentActionInfo;
		LatentActionInfo.CallbackTarget = this;
		UKismetSystemLibrary::MoveComponentTo(
			GetRootComponent(),
			SensorComponent->GetLedgeClimbingDownLocation(),
			GetActorRotation(),
			true, true, 0.05f, false,
			EMoveComponentAction::Type::Move,
			LatentActionInfo);
		
		SetActorRotation((GetActorForwardVector() * -1).Rotation());
		JumpToAnimationNode(JumpToRunNodeName);

		return;
	}

	if (IsLedgeHanging)
	{
		GetCharacterMovement()->GravityScale = DefaultGravityScale;

		return;
	}

	if (IsAttacking || IsSliding || !IsGrounded() || IsWallAbove()) return;
	
	Crouch();
	GetSprite()->SetRelativeLocation(CrouchedSpriteOffset);
	JumpToAnimationNode(JumpToCrouchNodeName);
}

void AWarrior::OnDownInputReleased()
{
	HasCrouchedInput = false;
	
	if (!bIsCrouched || IsSliding || IsAttacking || IsWallAbove()) return;
	
	UnCrouch();
	GetSprite()->SetRelativeLocation(DefaultSpriteOffset);
}

void AWarrior::Dash()
{
	if (IsAttacking || IsDashing || bIsCrouched || !IsGrounded()) return;
	
	IsDashing = true;
	GetCharacterMovement()->bCanWalkOffLedges = false;
	GetCharacterMovement()->AddImpulse(GetActorForwardVector() * DashSpeed + GetActorUpVector() * -1.0f * DefaultGravityScale, true);
	ComboComponent->ComboCheck(EComboInput::Dash);
}

void AWarrior::StopDashing()
{
	IsDashing = false;
	GetCharacterMovement()->bCanWalkOffLedges = true;
}

void AWarrior::OnEnterLocomotion()
{
	if (IsDashing)
	{
		JumpToAnimationNode(JumpToStopDashingNodeName);
	}
	else
	{
		ResetAction();
	}
}

void AWarrior::WallSlide()
{
	if (IsWallSliding) return;

	IsWallSliding = true;
	IsLedgeHanging = false;
	
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->GravityScale = WallSlideGravityScale;
	JumpToAnimationNode(JumpToWallSlideNodeName);
}

void AWarrior::WallJump()
{
	JumpToAnimationNode(JumpToJumpUpNodeName);
	
	const FVector HorizontalVelocity = GetActorForwardVector() * -1.0f * WallJumpVelocity.X;
	const FVector VerticalVelocity = GetActorUpVector() * WallJumpVelocity.Z;
	const FVector NewVelocity = HorizontalVelocity + VerticalVelocity;
	LaunchCharacter(NewVelocity, true, true);

	SetActorRotation((GetActorForwardVector() * -1.0).Rotation());
}

void AWarrior::LedgeGrab()
{
	if (IsLedgeHanging) return;

	IsLedgeHanging = true;
	GetCharacterMovement()->GravityScale = 0.0f;
	GetCharacterMovement()->StopMovementImmediately();
	JumpToAnimationNode(LedgeGrabNodeName);
	
	FLatentActionInfo LatentActionInfo;
	LatentActionInfo.CallbackTarget = this;
	UKismetSystemLibrary::MoveComponentTo(
		GetRootComponent(),
		SensorComponent->GetLedgeGrabLocation(),
		GetActorRotation(),
		false, false, 0.1f, false,
		EMoveComponentAction::Type::Move,
		LatentActionInfo);
}

bool AWarrior::WallSlideCheck()
{
	FHitResult HitResult;
	const FCollisionShape CheckShape = FCollisionShape::MakeCapsule(
		GetCapsuleComponent()->GetScaledCapsuleRadius() + WallSlideTolerance,
		GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Params;

	Params.AddIgnoredActor(this);
	GetWorld()->SweepSingleByChannel(
		HitResult,
		GetActorLocation(),
		GetActorLocation(),
		GetActorRotation().Quaternion(),
		ECC_Visibility,
		CheckShape,
		Params);

	if (HitResult.GetActor() == nullptr) return false;
	
	if (HitResult.Normal.X > 0.0f)
	{
		SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
	}
	else if (HitResult.Normal.X < 0.0f)
	{
		SetActorRotation(FRotator(0.0f));
	}
	
	return HitResult.GetActor()->ActorHasTag(WallTag) && HitResult.bBlockingHit;
}

#pragma endregion

#pragma region Overrides

void AWarrior::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	JumpToAnimationNode(JumpToJumpUpNodeName);
	SpriteScaleComponent->JumpSqueeze();
	IsAttacking = false;
}

void AWarrior::OnWalkingOffLedge_Implementation(const FVector& PreviousFloorImpactNormal,
	const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta)
{
	Super::OnWalkingOffLedge_Implementation(
		PreviousFloorImpactNormal,
		PreviousFloorContactNormal,
		PreviousLocation,
		TimeDelta);
	JumpToAnimationNode(JumpToFallNodeName);
}

void AWarrior::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (HasCrouchedInput)
	{
		Super::Crouch();
		GetSprite()->SetRelativeLocation(CrouchedSpriteOffset);
		JumpToAnimationNode(JumpToCrouchingNodeName);
	}
	else if (HasMoveInput)
	{
		JumpToAnimationNode(JumpToRunNodeName);
		IsAttacking = false;
	}
	else
	{
		SpriteScaleComponent->LandSquash();
		JumpToAnimationNode(JumpToLandAnimNodeName);
		IsAttacking = false;
	}
}

#pragma endregion

#pragma region Boolean Flags

bool AWarrior::IsGrounded() const
{
	return GetCharacterMovement()->IsMovingOnGround();
}

#pragma endregion

#pragma region Wall Check

bool AWarrior::IsWallAbove() const
{
	FHitResult HitResult;
	FCollisionQueryParams Params;

	Params.AddIgnoredActor(this);
	
	GetWorld()->LineTraceSingleByChannel(
		HitResult,
		GetActorLocation(),
		GetActorLocation() + FVector(0.0f, 0.0f, 100.0f),
		ECC_Visibility,
		Params);

	return HitResult.bBlockingHit && HitResult.GetActor()->ActorHasTag(WallTag);
}

#pragma endregion

#pragma region Jump To Animation Node

void AWarrior::JumpToAnimationNode(const FName JumpToNodeName, FName JumpToStateMachineName) const
{
	if (JumpToStateMachineName == NAME_None)
	{
		JumpToStateMachineName = LocomotionStateMachineName;
	}
	
	GetAnimationComponent()->GetAnimInstance()->JumpToNode(JumpToNodeName, JumpToStateMachineName);
}

#pragma endregion

#pragma region Public Usages

void AWarrior::ResetAction()
{
	IsAttacking = false;
	IsDashing = false;

	if (HasMoveInput)
	{
		JumpToAnimationNode(JumpToRunNodeName);
	}
	else
	{
		JumpToAnimationNode(JumpToIdleNodeName);
	}
}

#pragma endregion

#pragma region Others

#pragma endregion